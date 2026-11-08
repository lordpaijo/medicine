#define _GNU_SOURCE
#include "server.h"

#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include "../buffer/buf.h"
#include "../http/http.h"
#include "../markdown/markdown.h"
#include "../page/page.h"

#define MAX_MD (2 * 1024 * 1024)   

static char   g_root[PATH_MAX];   
static size_t g_rootlen;         

int server_init(const char *docroot) {
    if (!realpath(docroot, g_root)) { perror(docroot); return -1; }
    g_rootlen = strlen(g_root);
    if (g_rootlen == 1) g_rootlen = 0;              
    return 0;
}

const char *server_root(void) { return g_root; }

static int user_css_exists(void) {
    char p[PATH_MAX + 16];
    snprintf(p, sizeof p, "%.*s/style.css", (int)g_rootlen, g_root);
    return access(p, R_OK) == 0;
}

static void begin_page(Buf *o, const char *title) { page_begin(o, title, user_css_exists()); }

static void send_error(int fd, int code, const char *extra, int head) {
    Buf b = {0};
    char t[64];
    snprintf(t, sizeof t, "%d %s", code, reason_of(code));
    begin_page(&b, t);
    buf_printf(&b, "<h1>%d %s</h1>\n<p><a href=\"/\">&larr; Back to the start</a></p>\n", code, reason_of(code));
    page_end(&b);
    send_body(fd, code, "text/html; charset=utf-8", extra, b.d, b.len, head);
    free(b.d);
}

static const char *basename_of(const char *p) {
    const char *s = strrchr(p, '/');
    return s ? s + 1 : p;
}

static int is_markdown(const char *path) {
    const char *dot = strrchr(path, '.');
    return dot && (strcasecmp(dot, ".md") == 0 || strcasecmp(dot, ".markdown") == 0);
}

static void serve_file(int fd, const char *path, int head, int raw) {
    struct stat st;
    if (stat(path, &st) < 0 || !S_ISREG(st.st_mode)) { send_error(fd, 404, NULL, head); return; }

    if (!raw && is_markdown(path)) {
        if (st.st_size > MAX_MD) { send_error(fd, 413, NULL, head); return; }
        FILE *f = fopen(path, "rb");
        if (!f) { send_error(fd, 403, NULL, head); return; }
        char *text = xrealloc(NULL, (size_t)st.st_size + 1);
        size_t got = fread(text, 1, (size_t)st.st_size, f);
        fclose(f);
        text[got] = 0;

        Buf body = {0}, page = {0};
        char title[256];
        md_render(text, &body, title, sizeof title);
        begin_page(&page, title[0] ? title : basename_of(path));
        if (body.d) buf_putn(&page, body.d, body.len);
        page_end(&page);
        send_body(fd, 200, "text/html; charset=utf-8", NULL, page.d, page.len, head);
        free(text); free(body.d); free(page.d);
        return;
    }

    int ffd = open(path, O_RDONLY);
    if (ffd < 0) { send_error(fd, 403, NULL, head); return; }
    send_headers(fd, 200, raw ? "text/plain; charset=utf-8" : mime_of(path), (size_t)st.st_size, NULL);
    if (!head) {
        char chunk[16384];
        ssize_t r;
        while ((r = read(ffd, chunk, sizeof chunk)) > 0) send_all(fd, chunk, (size_t)r);
    }
    close(ffd);
}

static int filter_hidden(const struct dirent *e) { return e->d_name[0] != '.'; }

static void serve_dir(int fd, const char *path, const char *urlpath, int head) {
    struct dirent **list;
    int n = scandir(path, &list, filter_hidden, alphasort);
    if (n < 0) { send_error(fd, 403, NULL, head); return; }

    Buf b = {0};
    char title[PATH_MAX];
    snprintf(title, sizeof title, "Index of %s", urlpath);
    begin_page(&b, title);
    buf_puts(&b, "<h1>Index of ");
    esc(&b, urlpath);
    buf_puts(&b, "</h1>\n<ul class=\"dir-list\">\n");
    if (strcmp(urlpath, "/") != 0) buf_puts(&b, "<li><a href=\"../\">../</a></li>\n");
    for (int i = 0; i < n; i++) {
        char full[PATH_MAX * 2];
        struct stat st;
        snprintf(full, sizeof full, "%s/%s", path, list[i]->d_name);
        int isdir = stat(full, &st) == 0 && S_ISDIR(st.st_mode);
        buf_puts(&b, "<li><a href=\"");
        url_enc(&b, list[i]->d_name);
        if (isdir) buf_putc(&b, '/');
        buf_puts(&b, "\">");
        esc(&b, list[i]->d_name);
        if (isdir) buf_putc(&b, '/');
        buf_puts(&b, "</a></li>\n");
        free(list[i]);
    }
    free(list);
    buf_puts(&b, "</ul>\n");
    page_end(&b);
    send_body(fd, 200, "text/html; charset=utf-8", NULL, b.d, b.len, head);
    free(b.d);
}

static int hexv(int c) { return isdigit(c) ? c - '0' : (tolower(c) - 'a' + 10); }

static int url_decode(char *s) {
    char *o = s;
    for (; *s; s++) {
        if (*s == '%') {
            if (!isxdigit((unsigned char)s[1]) || !isxdigit((unsigned char)s[2])) return -1;
            int v = hexv((unsigned char)s[1]) * 16 + hexv((unsigned char)s[2]);
            if (v == 0) return -1;
            *o++ = (char)v;
            s += 2;
        } else {
            *o++ = *s;
        }
    }
    *o = 0;
    return 0;
}

static int resolve(const char *urlpath, char *out) {
    static const char *suffix[] = {"", ".md", ".html", NULL};
    char full[PATH_MAX * 2];
    for (int i = 0; suffix[i]; i++) {
        int w = snprintf(full, sizeof full, "%.*s%s%s", (int)g_rootlen, g_root, urlpath, suffix[i]);
        if (w < 0 || (size_t)w >= sizeof full) return -1;
        if (realpath(full, out)) {
            if (strncmp(out, g_root, g_rootlen) == 0 && (out[g_rootlen] == '/' || out[g_rootlen] == 0))
                return 0;
            return -1;
        }
    }
    return -1;
}

void server_handle(int fd, const char *ip) {
    char req[8192];
    size_t got = 0;
    while (got < sizeof req - 1) {
        ssize_t r = recv(fd, req + got, sizeof req - 1 - got, 0);
        if (r < 0 && errno == EINTR) continue;
        if (r <= 0) break;
        got += (size_t)r;
        req[got] = 0;
        if (strstr(req, "\r\n\r\n") || strstr(req, "\n\n")) break;
    }
    if (!got) return;
    req[got] = 0;

    char method[16], target[2048], ver[16];
    http_reset_status();
    if (sscanf(req, "%15s %2047s %15s", method, target, ver) != 3 || target[0] != '/') {
        send_error(fd, 400, NULL, 0);
        fprintf(stderr, "%s - bad request -> %d\n", ip, http_last_status());
        return;
    }

    int head = strcmp(method, "HEAD") == 0;
    if (!head && strcmp(method, "GET") != 0) {
        send_error(fd, 405, "Allow: GET, HEAD\r\n", 0);
        fprintf(stderr, "%s \"%s %s\" -> %d\n", ip, method, target, http_last_status());
        return;
    }

    char orig[2048];
    char *query = strchr(target, '?');
    if (query) *query++ = 0;
    snprintf(orig, sizeof orig, "%s", target);
    int raw = query && strcmp(query, "raw") == 0;

    if (strcmp(target, "/_mdserve/style.css") == 0) {
        send_body(fd, 200, "text/css; charset=utf-8", NULL, page_css(), strlen(page_css()), head);
        goto done;
    }

    if (url_decode(target) < 0 || strstr(target, "/.") != NULL) {
        send_error(fd, strstr(target, "/.") ? 403 : 400, NULL, head);
        goto done;
    }

    char path[PATH_MAX];
    if (resolve(target, path) < 0) { send_error(fd, 404, NULL, head); goto done; }

    struct stat st;
    if (stat(path, &st) < 0) { send_error(fd, 404, NULL, head); goto done; }

    if (S_ISDIR(st.st_mode)) {
        size_t ol = strlen(orig);
        if (orig[ol - 1] != '/') {
            const char *o = orig;
            while (o[1] == '/') o++;
            char loc[2200];
            snprintf(loc, sizeof loc, "Location: %s/\r\n", o);
            send_body(fd, 301, "text/plain", loc, "", 0, head);
            goto done;
        }
        static const char *index_names[] = {"index.md", "README.md", "readme.md", "index.html", NULL};
        for (int i = 0; index_names[i]; i++) {
            char cand[PATH_MAX * 2];
            struct stat cs;
            snprintf(cand, sizeof cand, "%s/%s", path, index_names[i]);
            if (stat(cand, &cs) == 0 && S_ISREG(cs.st_mode)) { serve_file(fd, cand, head, raw); goto done; }
        }
        serve_dir(fd, path, target, head);
    } else if (S_ISREG(st.st_mode)) {
        serve_file(fd, path, head, raw);
    } else {
        send_error(fd, 404, NULL, head);
    }

done:
    fprintf(stderr, "%s \"%s %s\" -> %d\n", ip, method, orig, http_last_status());
}
