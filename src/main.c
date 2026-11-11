/* main.c - medicine: a tiny Markdown-rendering HTTP server
 *
 * Build:  make
 * Run:    ./build/medicine [-p port] [-b bind-address] [docroot]
 *
 * Unix: one forked process per connection. Windows: one thread per connection.
 * GET/HEAD only, no TLS, no keep-alive. Hobby project: don't put it on the
 * open internet.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "modules/platform/platform.h"
#include "modules/server/server.h"

static void usage(const char *argv0) {
    fprintf(stderr,
        "usage: %s [-p port] [-b bind-address] [docroot]\n"
        "  -p PORT   port to listen on        (default 8080)\n"
        "  -b ADDR   IPv4 address to bind to  (default 127.0.0.1)\n"
        "  docroot   directory to serve       (default .)\n", argv0);
}

int main(int argc, char **argv) {
    int port = 8080;
    const char *bind_addr = "127.0.0.1";
    const char *docroot = ".";

    plat_args(&argc, &argv);

    for (int i = 1; i < argc; i++) {
        const char *a = argv[i];
        if (strcmp(a, "-h") == 0 || strcmp(a, "--help") == 0) {
            usage(argv[0]);
            return 0;
        } else if (a[0] == '-' && (a[1] == 'p' || a[1] == 'b')) {
            const char *val = a[2] ? a + 2 : (i + 1 < argc ? argv[++i] : NULL);
            if (!val) { usage(argv[0]); return 1; }
            if (a[1] == 'p') port = atoi(val);
            else bind_addr = val;
        } else if (a[0] == '-' && a[1]) {
            fprintf(stderr, "unknown option: %s\n", a);
            usage(argv[0]);
            return 1;
        } else {
            docroot = a;
        }
    }
    if (port < 1 || port > 65535) { fprintf(stderr, "bad port\n"); return 1; }

    if (plat_init() != 0) return 1;
    if (server_init(docroot) < 0) return 1;

    sock_t srv = plat_listen(bind_addr, port);
    if (srv == SOCK_BAD) return 1;

    fprintf(stderr, "medicine: serving %s on http://%s:%d/\n", server_root(), bind_addr, port);
    plat_serve(srv, server_handle);
    return 0;
}
