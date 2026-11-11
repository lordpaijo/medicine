#include "server.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "../buffer/buf.h"
#include "../http/http.h"
#include "../markdown/markdown.h"
#include "../page/page.h"

#define MAX_MD          (2 * 1024 * 1024)  /* refuse to render bigger Markdown files */
#define REQUEST_TIMEOUT 30                 /* seconds allowed to receive a request */

static char   g_root[PLAT_PATH_MAX];       /* canonical document root, '/' separators */
static size_t g_rootlen;                   /* strlen(g_root), or 0 when root is "/" */

int server_init(const char *docroot) {
    PlatStat st;
    if (!plat_realpath(docroot, g_root) || plat_stat(g_root, &st) < 0 || !st.is_dir) {
        fprintf(stderr, "%s: not a usable directory\n", docroot);
        return -1;
    }
    g_rootlen = strlen(g_root);
    /* "C:/" -> "C:" so that root + "/docs" works; plain "/" becomes length 0 */
    while (g_rootlen > 1 && g_root[g_rootlen - 1] == '/') g_root[--g_rootlen] = 0;
    if (g_rootlen == 1 && g_root[0] == '/') g_rootlen = 0;
    return 0;
}

const char *server_root(void) { return g_root; }

static int user_css_exists(void) {
    char p[PLAT_PATH_MAX + 16];
    PlatStat st;
    snprintf(p, sizeof p, "%.*s/style.css", (int)g_rootlen, g_root);
    return plat_stat(p, &st) == 0 && st.is_file;
}

static void begin_page(Buf *o, const char *title) { page_begin(o, title, user_css_exists()); }

static void send_error(sock_t fd, int code, const char *extra, int head) {
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
    return dot && (ci_cmp(dot, ".md") == 0 || ci_cmp(dot, ".markdown") == 0);
}

static void serve_file(sock_t fd, const char *path, int head, int raw) {
    PlatStat st;
    if (plat_stat(path, &st) < 0 || !st.is_file) { send_error(fd, 404, NULL, head); return; }
    FILE *f = plat_fopen_rb(path);
    if (!f) { send_error(fd, 403, NULL, head); return; }

    if (!raw && is_markdown(path)) {
        if (st.size > MAX_MD) { fclose(f); send_error(fd, 413, NULL, head); return; }
        char *text = xrealloc(NULL, (size_t)st.size + 1);
        size_t got = fread(text, 1, (size_t)st.size, f);
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

    send_headers(fd, 200, raw ? "text/plain; charset=utf-8" : mime_of(path),
                 (unsigned long long)st.size, NULL);
    if (!head) {
        char chunk[16384];
        size_t r;
        while ((r = fread(chunk, 1, sizeof chunk, f)) > 0) send_all(fd, chunk, r);
    }
    fclose(f);
}

typedef struct { char *name; int is_dir; } Entry;
typedef struct { Entry *v; int n, cap; } Entries;

static void collect(const char *name, int is_dir, void *user) {
    Entries *E = user;
    if (E->n == E->cap) {
        E->cap = E->cap ? E->cap * 2 : 32;
        E->v = xrealloc(E->v, (size_t)E->cap * sizeof *E->v);
    }
    E->v[E->n].name = xstrndup(name, strlen(name));
    E->v[E->n].is_dir = is_dir;
    E->n++;
}

static int entry_cmp(const void *a, const void *b) {
    const Entry *x = a, *y = b;
    int c = ci_cmp(x->name, y->name);
    return c ? c : strcmp(x->name, y->name);
}

static void serve_dir(sock_t fd, const char *path, const char *urlpath, int head) {
    Entries E = {0};
    if (plat_listdir(path, collect, &E) < 0) { send_error(fd, 403, NULL, head); return; }
    qsort(E.v, (size_t)E.n, sizeof *E.v, entry_cmp);

    Buf b = {0};
    char title[PLAT_PATH_MAX];
    snprintf(title, sizeof title, "Index of %s", urlpath);
    begin_page(&b, title);
    buf_puts(&b, "<h1>Index of ");
    esc(&b, urlpath);
    buf_puts(&b, "</h1>\n<ul class=\"dir-list\">\n");
    if (strcmp(urlpath, "/") != 0) buf_puts(&b, "<li><a href=\"../\">../</a></li>\n");
    for (int i = 0; i < E.n; i++) {
        buf_puts(&b, "<li><a href=\"");
        url_enc(&b, E.v[i].name);
        if (E.v[i].is_dir) buf_putc(&b, '/');
        buf_puts(&b, "\">");
        esc(&b, E.v[i].name);
        if (E.v[i].is_dir) buf_putc(&b, '/');
        buf_puts(&b, "</a></li>\n");
        free(E.v[i].name);
    }
    free(E.v);
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
            if (v == 0) return -1;                  /* %00 would truncate the path */
            *o++ = (char)v;
            s += 2;
        } else {
            *o++ = *s;
        }
    }
    *o = 0;
    return 0;
}

/* Map a URL path to a real file inside the docroot, trying /x, /x.md, /x.html. */
static int resolve(const char *urlpath, char *out) {
    static const char *suffix[] = {"", ".md", ".html", NULL};
    char full[PLAT_PATH_MAX * 2];
    for (int i = 0; suffix[i]; i++) {
        int w = snprintf(full, sizeof full, "%.*s%s%s", (int)g_rootlen, g_root, urlpath, suffix[i]);
        if (w < 0 || (size_t)w >= sizeof full) return -1;
        if (plat_realpath(full, out))              /* resolves symlinks, so ".." tricks fail here */
            return plat_inside(out, g_root, g_rootlen) ? 0 : -1;
    }
    return -1;
}

/* Decide what to send for one request. `target` is the path (query already
 * removed, still percent-encoded); `orig` is an untouched copy of it. */
static void route(sock_t fd, char *target, const char *orig, int head, int raw) {
    if (strcmp(target, "/_mdserve/style.css") == 0) {
        send_body(fd, 200, "text/css; charset=utf-8", NULL, page_css(), strlen(page_css()), head);
        return;
    }

    if (url_decode(target) < 0) { send_error(fd, 400, NULL, head); return; }

    /* "/." catches ".." and dotfiles (.git, .env); plat_path_unsafe adds the
     * Windows-only dangers (backslashes, "C:", NUL, COM1, ...). */
    if (strstr(target, "/.") != NULL || plat_path_unsafe(target)) {
        send_error(fd, 403, NULL, head);
        return;
    }

    char path[PLAT_PATH_MAX];
    PlatStat st;
    if (resolve(target, path) < 0 || plat_stat(path, &st) < 0) { send_error(fd, 404, NULL, head); return; }

    if (!st.is_dir) { serve_file(fd, path, head, raw); return; }

    /* directory without a trailing slash: redirect, so relative links work */
    if (orig[strlen(orig) - 1] != '/') {
        const char *o = orig;
        while (o[1] == '/') o++;                   /* avoid //host open redirects */
        char loc[2200];
        snprintf(loc, sizeof loc, "Location: %s/\r\n", o);
        send_body(fd, 301, "text/plain", loc, "", 0, head);
        return;
    }

    static const char *index_names[] = {"index.md", "README.md", "readme.md", "index.html", NULL};
    for (int i = 0; index_names[i]; i++) {
        char cand[PLAT_PATH_MAX * 2];
        PlatStat cs;
        snprintf(cand, sizeof cand, "%s/%s", path, index_names[i]);
        if (plat_stat(cand, &cs) == 0 && cs.is_file) { serve_file(fd, cand, head, raw); return; }
    }
    serve_dir(fd, path, target, head);
}

void server_handle(sock_t fd, const char *ip) {
    char req[8192];
    size_t got = 0;
    time_t t0 = time(NULL);
    while (got < sizeof req - 1) {
        int r = plat_recv(fd, req + got, sizeof req - 1 - got);
        if (r <= 0) break;
        got += (size_t)r;
        req[got] = 0;
        if (strstr(req, "\r\n\r\n") || strstr(req, "\n\n")) break;
        if (time(NULL) - t0 > REQUEST_TIMEOUT) break;   /* slow-trickle clients */
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

    route(fd, target, orig, head, raw);
    fprintf(stderr, "%s \"%s %s\" -> %d\n", ip, method, orig, http_last_status());
}
