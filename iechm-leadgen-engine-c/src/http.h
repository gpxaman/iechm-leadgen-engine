/* Thin REST + static-file server -- mirrors server.py. No business logic:
 * every route just calls a library_* function and returns its JSON, or
 * serves a file from ui_dir. One detached pthread per accepted connection,
 * matching the original's ThreadingHTTPServer; each connection handles
 * exactly one request (no keep-alive) which keeps the parser simple and is
 * a non-issue for a local single-user dashboard. */
#ifndef IECHM_HTTP_H
#define IECHM_HTTP_H

/* Blocks forever accepting connections, like Python's serve_forever(). */
void http_serve(int port, const char *ui_dir);

#endif
