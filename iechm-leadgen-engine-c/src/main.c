/* Server entry point -- mirrors server.py's main(). Resolves ui/ and
 * iechm.db relative to the running binary's own location (like Python's
 * Path(__file__).resolve().parent), so it works regardless of the caller's
 * current directory.
 *
 * Run:
 *     ./iechm_server [port]
 * Then open http://localhost:8000/
 */
#include "library.h"
#include "http.h"
#include "rng.h"
#include "common.h"
#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <unistd.h>
#include <string.h>

static void get_exe_dir(char *out, size_t cap) {
    char path[4096];
    ssize_t n = readlink("/proc/self/exe", path, sizeof path - 1);
    if (n <= 0) { xcpy(out, cap, "."); return; }
    path[n] = '\0';
    char *slash = strrchr(path, '/');
    if (slash) *slash = '\0';
    xcpy(out, cap, path);
}

int main(int argc, char **argv) {
    signal(SIGPIPE, SIG_IGN); /* a client closing mid-response shouldn't kill the server */
    global_rng_seed();

    int port = argc > 1 ? atoi(argv[1]) : 8000;

    char exe_dir[2048], ui_dir[2048], db_path[2048];
    get_exe_dir(exe_dir, sizeof exe_dir);
    snprintf(ui_dir, sizeof ui_dir, "%s/ui", exe_dir);
    snprintf(db_path, sizeof db_path, "%s/iechm.db", exe_dir);

    library_init(db_path);

    printf("IECHM lead-gen dashboard: http://localhost:%d/\n", port);
    printf("API base: /api/  (channels, leads, agents, sentinel-events, strategies, funnel, status, run-cycle, reset)\n");
    fflush(stdout);

    http_serve(port, ui_dir); /* blocks forever */
    return 0;
}
