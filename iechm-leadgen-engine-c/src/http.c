#include "http.h"
#include "library.h"
#include "common.h"
#include "strutil.h"
#include "jsonw.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <netinet/in.h>
#include <limits.h>

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

#define MAX_HEADER 8192
#define MAX_BODY 1048576

static char g_ui_dir[1024];

typedef struct {
    char method[8];
    char path[1024];
    char query[1024];
    char *body; /* malloc'd, NUL-terminated, never NULL (empty string if none) */
} HttpRequest;

/* ------------------------------------------------------------- request parsing */

static long find_crlfcrlf(const char *buf, long len) {
    for (long i = 0; i + 3 < len; i++)
        if (buf[i] == '\r' && buf[i + 1] == '\n' && buf[i + 2] == '\r' && buf[i + 3] == '\n') return i;
    return -1;
}

static bool read_request(int fd, HttpRequest *req) {
    char *buf = NULL;
    size_t cap = 0;
    long len = 0, header_end = -1;

    for (;;) {
        if ((size_t) len == cap) {
            cap = cap ? cap * 2 : 4096;
            char *nb = realloc(buf, cap);
            if (!nb) { free(buf); return false; }
            buf = nb;
        }
        ssize_t n = recv(fd, buf + len, cap - (size_t) len, 0);
        if (n <= 0) { free(buf); return false; }
        len += n;
        header_end = find_crlfcrlf(buf, len);
        if (header_end >= 0) break;
        if (len > MAX_HEADER) { free(buf); return false; }
    }
    long body_start = header_end + 4;

    long header_len = header_end < MAX_HEADER ? header_end : MAX_HEADER;
    char header_buf[MAX_HEADER + 1];
    memcpy(header_buf, buf, (size_t) header_len);
    header_buf[header_len] = '\0';

    memset(req, 0, sizeof *req);
    char raw_path[1024] = "";
    if (sscanf(header_buf, "%7s %1023s", req->method, raw_path) != 2) { free(buf); return false; }

    char *qmark = strchr(raw_path, '?');
    if (qmark) {
        *qmark = '\0';
        xcpy(req->query, sizeof req->query, qmark + 1);
    } else {
        req->query[0] = '\0';
    }
    xcpy(req->path, sizeof req->path, raw_path);

    long content_length = 0;
    int cl_pos = ci_find(header_buf, "content-length:", 0);
    if (cl_pos >= 0) {
        int cursor = cl_pos + (int) strlen("content-length:");
        while (header_buf[cursor] == ' ') cursor++;
        content_length = atol(header_buf + cursor);
    }
    if (content_length < 0) content_length = 0;
    if (content_length > MAX_BODY) content_length = MAX_BODY;

    long have_body = len - body_start;
    while (have_body < content_length) {
        if ((size_t) len == cap) {
            cap *= 2;
            char *nb = realloc(buf, cap);
            if (!nb) { free(buf); return false; }
            buf = nb;
        }
        ssize_t n = recv(fd, buf + len, cap - (size_t) len, 0);
        if (n <= 0) break;
        len += n;
        have_body = len - body_start;
    }
    if (have_body < 0) have_body = 0;
    if (have_body > content_length) have_body = content_length;

    req->body = malloc((size_t) have_body + 1);
    if (have_body > 0) memcpy(req->body, buf + body_start, (size_t) have_body);
    req->body[have_body] = '\0';

    free(buf);
    return true;
}

/* ------------------------------------------------------------------ responses */

static void send_response(int fd, int status, const char *status_text, const char *content_type,
                           const char *body, size_t body_len, bool cors) {
    char header[512];
    int n = snprintf(header, sizeof header,
                      "HTTP/1.1 %d %s\r\n"
                      "Content-Type: %s\r\n"
                      "Content-Length: %zu\r\n"
                      "%s"
                      "Connection: close\r\n"
                      "\r\n",
                      status, status_text, content_type, body_len,
                      cors ? "Access-Control-Allow-Origin: *\r\n" : "");
    send(fd, header, (size_t) n, MSG_NOSIGNAL);
    if (body_len) send(fd, body, body_len, MSG_NOSIGNAL);
}

static char *error_json(const char *msg) {
    Jsonw w;
    jw_init(&w);
    jw_obj_open(&w);
    jw_kv_str(&w, "error", msg);
    jw_obj_close(&w);
    return jw_finish(&w);
}

static void send_json_owned(int fd, int status, char *json_owned) {
    const char *status_text = status == 200 ? "OK" : status == 404 ? "Not Found" : status == 500 ? "Internal Server Error" : "Error";
    send_response(fd, status, status_text, "application/json; charset=utf-8", json_owned, strlen(json_owned), true);
    free(json_owned);
}

static void send_not_found_json(int fd) {
    char *b = error_json("not found");
    send_response(fd, 404, "Not Found", "application/json; charset=utf-8", b, strlen(b), true);
    free(b);
}

/* --------------------------------------------------------------- static files */

static const char *content_type_for(const char *path) {
    const char *dot = strrchr(path, '.');
    if (!dot) return "application/octet-stream";
    if (strcmp(dot, ".html") == 0) return "text/html; charset=utf-8";
    if (strcmp(dot, ".css") == 0) return "text/css; charset=utf-8";
    if (strcmp(dot, ".js") == 0) return "application/javascript; charset=utf-8";
    if (strcmp(dot, ".json") == 0) return "application/json; charset=utf-8";
    return "application/octet-stream";
}

static void serve_static(int fd, const char *req_path) {
    char rel[1024];
    const char *p = req_path;
    if (*p == '/') p++;
    if (*p == '\0') p = "index.html";
    xcpy(rel, sizeof rel, p);

    if (strstr(rel, "..")) {
        char *b = error_json("forbidden");
        send_response(fd, 403, "Forbidden", "application/json; charset=utf-8", b, strlen(b), true);
        free(b);
        return;
    }

    char full[2048];
    snprintf(full, sizeof full, "%s/%s", g_ui_dir, rel);

    char real_ui[PATH_MAX], real_full[PATH_MAX];
    if (!realpath(g_ui_dir, real_ui)) {
        char *b = error_json("server misconfigured");
        send_response(fd, 500, "Internal Server Error", "application/json; charset=utf-8", b, strlen(b), true);
        free(b);
        return;
    }
    if (!realpath(full, real_full)) { send_not_found_json(fd); return; }

    size_t ui_len = strlen(real_ui);
    if (strncmp(real_full, real_ui, ui_len) != 0 || (real_full[ui_len] != '/' && real_full[ui_len] != '\0')) {
        char *b = error_json("forbidden");
        send_response(fd, 403, "Forbidden", "application/json; charset=utf-8", b, strlen(b), true);
        free(b);
        return;
    }

    struct stat st;
    if (stat(real_full, &st) != 0 || !S_ISREG(st.st_mode)) { send_not_found_json(fd); return; }

    FILE *f = fopen(real_full, "rb");
    if (!f) { send_not_found_json(fd); return; }
    size_t size = (size_t) st.st_size;
    char *data = malloc(size ? size : 1);
    size_t got = fread(data, 1, size, f);
    fclose(f);

    send_response(fd, 200, "OK", content_type_for(real_full), data, got, false);
    free(data);
}

/* --------------------------------------------------------------- query/body */

static void url_decode(const char *src, char *dst, size_t cap) {
    size_t w = 0;
    for (const char *p = src; *p && w + 1 < cap;) {
        if (*p == '%' && isxdigit((unsigned char) p[1]) && isxdigit((unsigned char) p[2])) {
            char hex[3] = {p[1], p[2], 0};
            dst[w++] = (char) strtol(hex, NULL, 16);
            p += 3;
        } else if (*p == '+') {
            dst[w++] = ' ';
            p++;
        } else {
            dst[w++] = *p++;
        }
    }
    dst[w] = '\0';
}

static bool query_get(const char *query, const char *key, char *out, size_t outcap) {
    size_t klen = strlen(key);
    const char *p = query;
    while (p && *p) {
        const char *amp = strchr(p, '&');
        const char *seg_end = amp ? amp : p + strlen(p);
        const char *eq = memchr(p, '=', (size_t) (seg_end - p));
        if (eq && (size_t) (eq - p) == klen && strncmp(p, key, klen) == 0) {
            char raw[1024];
            size_t vlen = (size_t) (seg_end - (eq + 1));
            if (vlen >= sizeof raw) vlen = sizeof raw - 1;
            memcpy(raw, eq + 1, vlen);
            raw[vlen] = '\0';
            url_decode(raw, out, outcap);
            return true;
        }
        p = amp ? amp + 1 : NULL;
    }
    return false;
}

/* Ad hoc extraction of an optional "day" integer field from a POST body
 * shaped like {"day": 5} or {} -- the only JSON shape this API ever
 * receives, so a full parser would be pure overhead. */
static void parse_day_field(const char *body, bool *has_day, int *day) {
    *has_day = false;
    *day = 0;
    int pos = ci_find(body, "\"day\"", 0);
    if (pos < 0) return;
    int cursor = pos + 5;
    while (body[cursor] == ' ' || body[cursor] == '\t') cursor++;
    if (body[cursor] != ':') return;
    cursor++;
    while (body[cursor] == ' ' || body[cursor] == '\t') cursor++;
    if (strncmp(body + cursor, "null", 4) == 0) return;

    int sign = 1;
    if (body[cursor] == '-') { sign = -1; cursor++; }
    int start = cursor;
    long val = 0;
    while (isdigit((unsigned char) body[cursor])) { val = val * 10 + (body[cursor] - '0'); cursor++; }
    if (cursor == start) return;
    *day = (int) (sign * val);
    *has_day = true;
}

/* -------------------------------------------------------------------- routing */

static void route_get(int fd, const char *path, const char *query) {
    if (strcmp(path, "/api/channels") == 0) { send_json_owned(fd, 200, library_list_channel_types()); return; }

    if (strcmp(path, "/api/leads") == 0) {
        char limit_s[32];
        int limit = 100;
        if (query_get(query, "limit", limit_s, sizeof limit_s)) limit = atoi(limit_s);
        char status[SHORT_LEN] = "";
        query_get(query, "status", status, sizeof status);
        char ctype[SHORT_LEN] = "";
        query_get(query, "channel_type", ctype, sizeof ctype);
        send_json_owned(fd, 200, library_list_leads(limit, status[0] ? status : NULL, ctype[0] ? ctype : NULL));
        return;
    }

    if (strncmp(path, "/api/leads/", 11) == 0) {
        const char *lead_id = path + 11;
        char *detail = library_get_lead(lead_id);
        if (!detail) { send_not_found_json(fd); return; }
        send_json_owned(fd, 200, detail);
        return;
    }

    if (strcmp(path, "/api/agents") == 0) { send_json_owned(fd, 200, library_list_agents()); return; }

    if (strcmp(path, "/api/sentinel-events") == 0) {
        char limit_s[32];
        int limit = 50;
        if (query_get(query, "limit", limit_s, sizeof limit_s)) limit = atoi(limit_s);
        send_json_owned(fd, 200, library_list_sentinel_events(limit));
        return;
    }

    if (strcmp(path, "/api/strategies") == 0) { send_json_owned(fd, 200, library_list_strategies()); return; }
    if (strcmp(path, "/api/funnel") == 0) { send_json_owned(fd, 200, library_funnel_metrics()); return; }

    if (strcmp(path, "/api/status") == 0) {
        Jsonw w;
        jw_init(&w);
        jw_obj_open(&w);
        jw_kv_int(&w, "current_day", library_current_day());
        jw_obj_close(&w);
        send_json_owned(fd, 200, jw_finish(&w));
        return;
    }

    serve_static(fd, path);
}

static void route_post(int fd, const char *path, const char *body) {
    if (strcmp(path, "/api/run-cycle") == 0) {
        bool has_day = false;
        int day = 0;
        parse_day_field(body, &has_day, &day);
        send_json_owned(fd, 200, library_run_cycle(has_day, day));
        return;
    }

    if (strcmp(path, "/api/reset") == 0) {
        library_reset_all();
        Jsonw w;
        jw_init(&w);
        jw_obj_open(&w);
        jw_kv_bool(&w, "ok", true);
        jw_obj_close(&w);
        send_json_owned(fd, 200, jw_finish(&w));
        return;
    }

    send_not_found_json(fd);
}

static void route_options(int fd) {
    char header[256];
    int n = snprintf(header, sizeof header,
                      "HTTP/1.1 204 No Content\r\n"
                      "Access-Control-Allow-Origin: *\r\n"
                      "Access-Control-Allow-Methods: GET, POST, OPTIONS\r\n"
                      "Access-Control-Allow-Headers: Content-Type\r\n"
                      "Connection: close\r\n"
                      "\r\n");
    send(fd, header, (size_t) n, MSG_NOSIGNAL);
}

/* --------------------------------------------------------------- connections */

static void *handle_connection(void *arg) {
    int fd = *(int *) arg;
    free(arg);

    HttpRequest req;
    if (read_request(fd, &req)) {
        if (strcmp(req.method, "GET") == 0) route_get(fd, req.path, req.query);
        else if (strcmp(req.method, "POST") == 0) route_post(fd, req.path, req.body);
        else if (strcmp(req.method, "OPTIONS") == 0) route_options(fd);
        else send_not_found_json(fd);
        free(req.body);
    }
    close(fd);
    return NULL;
}

void http_serve(int port, const char *ui_dir) {
    xcpy(g_ui_dir, sizeof g_ui_dir, ui_dir);

    int listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (listen_fd < 0) { perror("socket"); exit(1); }

    int opt = 1;
    setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof opt);

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof addr);
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons((uint16_t) port);

    if (bind(listen_fd, (struct sockaddr *) &addr, sizeof addr) != 0) { perror("bind"); exit(1); }
    if (listen(listen_fd, 64) != 0) { perror("listen"); exit(1); }

    for (;;) {
        int *client_fd = malloc(sizeof(int));
        *client_fd = accept(listen_fd, NULL, NULL);
        if (*client_fd < 0) { free(client_fd); continue; }

        pthread_t tid;
        if (pthread_create(&tid, NULL, handle_connection, client_fd) != 0) {
            close(*client_fd);
            free(client_fd);
            continue;
        }
        pthread_detach(tid);
    }
}
