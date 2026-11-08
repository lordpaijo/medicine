/*
 * mdserve - a tiny HTTP server in C that renders Markdown as HTML.
 *
 * Build:  gcc -O2 -Wall -Wextra -o mdserve mdserve.c
 * Run:    ./mdserve [-p port] [-b bind-address] [docroot]
 *         ./mdserve -p 8080 ./site
 *
 * What it does:
 *   - *.md / *.markdown files are rendered to HTML with a built-in stylesheet
 *     (add ?raw to the URL to see the Markdown source instead)
 *   - everything else (.html, .css, .js, images, ...) is served as-is
 *   - directories serve index.md, README.md or index.html, or a file listing
 *   - /foo falls back to /foo.md or /foo.html
 *   - if docroot contains a style.css it is loaded after the built-in CSS,
 *     so you can restyle rendered pages without touching this file
 *
 * Markdown supported: headings, paragraphs, **bold**, *italic*, ~~strike~~,
 * `code`, fenced code blocks, links, images (and image links, e.g. badges),
 * main.c - mdserve: a tiny Markdown-rendering HTTP server
 *
 * Build:  make
 * Run:    ./mdserve [-p port] [-b bind-address] [docroot]
 *
 * One forked process per connection, GET/HEAD only, no TLS, no keep-alive.
 * Hobby project: don't put it on the open internet.
 */
#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>

#include "modules/server/server.h"

static void usage(const char *argv0) {
    fprintf(stderr, "usage: %s [-p port] [-b bind-address] [docroot]\n"
                    "  defaults: port 8080, bind 127.0.0.1, docroot .\n", argv0);
}

int main(int argc, char **argv) {
    int port = 8080, opt;
    const char *bind_addr = "127.0.0.1";

    while ((opt = getopt(argc, argv, "p:b:h")) != -1) {
        switch (opt) {
        case 'p': port = atoi(optarg); break;
        case 'b': bind_addr = optarg; break;
        default:  usage(argv[0]); return opt == 'h' ? 0 : 1;
        }
    }
    const char *docroot = optind < argc ? argv[optind] : ".";
    if (port < 1 || port > 65535) { fprintf(stderr, "bad port\n"); return 1; }
    if (server_init(docroot) < 0) return 1;

    signal(SIGCHLD, SIG_IGN);
    signal(SIGPIPE, SIG_IGN);

    int srv = socket(AF_INET, SOCK_STREAM, 0);
    if (srv < 0) { perror("socket"); return 1; }
    int one = 1;
    setsockopt(srv, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one);

    struct sockaddr_in addr = {0};
    addr.sin_family = AF_INET;
    addr.sin_port = htons((uint16_t)port);
    if (inet_pton(AF_INET, bind_addr, &addr.sin_addr) != 1) { fprintf(stderr, "bad address\n"); return 1; }
    if (bind(srv, (struct sockaddr *)&addr, sizeof addr) < 0) { perror("bind"); return 1; }
    if (listen(srv, 64) < 0) { perror("listen"); return 1; }

    fprintf(stderr, "mdserve: serving %s on http://%s:%d/\n", server_root(), bind_addr, port);

    for (;;) {
        struct sockaddr_in ca;
        socklen_t cl = sizeof ca;
        int c = accept(srv, (struct sockaddr *)&ca, &cl);
        if (c < 0) { if (errno != EINTR) perror("accept"); continue; }

        pid_t pid = fork();
        if (pid == 0) {
            close(srv);
            alarm(30);
            struct timeval tv = {10, 0};
            setsockopt(c, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);
            char ip[INET_ADDRSTRLEN];
            inet_ntop(AF_INET, &ca.sin_addr, ip, sizeof ip);
            server_handle(c, ip);
            close(c);
            _exit(0);
        }
        close(c);
    }
}
