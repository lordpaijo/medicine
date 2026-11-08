#define _GNU_SOURCE
#include "markdown.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#define MAXC      32   
#define MAX_DEPTH 24  

static char g_title[256];   
static void render_inline(Buf *o, const char *s, size_t n, int in_link);
static long find_code_close(const char *s, size_t n, size_t from, size_t r) {
    for (size_t j = from; j < n;) {
        if (s[j] == '`') {
            size_t k = 0;
            while (j + k < n && s[j + k] == '`') k++;
            if (k == r) return (long)j;
            j += k;
        } else j++;
    }
    return -1;
}

static long match_bracket(const char *s, size_t n, size_t i) {
    int depth = 0;
    for (; i < n; i++) {
        if (s[i] == '\\' && i + 1 < n) { i++; continue; }
        if (s[i] == '`') {
            size_t r = 0;
            while (i + r < n && s[i + r] == '`') r++;
            long cl = find_code_close(s, n, i + r, r);
            i = cl >= 0 ? (size_t)cl + r - 1 : i + r - 1;
            continue;
        }
        if (s[i] == '[') depth++;
        else if (s[i] == ']' && --depth == 0) return (long)i;
    }
    return -1;
}

static long match_paren(const char *s, size_t n, size_t i) {
    int depth = 0;
    for (; i < n; i++) {
        if (s[i] == '\\' && i + 1 < n) { i++; continue; }
        if (s[i] == '(') depth++;
        else if (s[i] == ')' && --depth == 0) return (long)i;
    }
    return -1;
}

static long find_closer(const char *s, size_t n, size_t from, char c, size_t r) {
    for (size_t j = from; j < n; j++) {
        if (s[j] == '\\') { j++; continue; }
        if (s[j] == '`') {
            size_t k = 0;
            while (j + k < n && s[j + k] == '`') k++;
            long cl = find_code_close(s, n, j + k, k);
            j = cl >= 0 ? (size_t)cl + k - 1 : j + k - 1;
            continue;
        }
        if (s[j] == c) {
            size_t k = 0;
            while (j + k < n && s[j + k] == c) k++;
            if (k == r && j > from && !isspace((unsigned char)s[j - 1])) {
                if (c == '_' && j + k < n && isalnum((unsigned char)s[j + k])) {
                    j += k - 1;
                    continue;
                }
                return (long)j;
            }
            j += k - 1;
        }
    }
    return -1;
}

static void emit_url(Buf *o, const char *u, size_t n, int is_image) {
    while (n && isspace((unsigned char)*u)) { u++; n--; }
    if (n >= 11 && strncasecmp(u, "javascript:", 11) == 0) { buf_putc(o, '#'); return; }
    if (n >= 5 && strncasecmp(u, "data:", 5) == 0 && !is_image) { buf_putc(o, '#'); return; }
    char *s = xstrndup(u, n);
    char *blob;
    if (is_image && strncmp(s, "https://github.com/", 19) == 0 && (blob = strstr(s, "/blob/"))) {
        *blob = 0;
        esc(o, s);
        buf_puts(o, "/raw/");
        esc(o, blob + 6);
    } else {
        esc(o, s);
    }
    free(s);
}

static int try_link(Buf *o, const char *s, size_t n, size_t lb, int image, size_t *next) {
    long rb = match_bracket(s, n, lb);
    if (rb < 0 || (size_t)rb + 1 >= n || s[rb + 1] != '(') return 0;
    long rp = match_paren(s, n, (size_t)rb + 1);
    if (rp < 0) return 0;

    const char *d = s + rb + 2;
    size_t dn = (size_t)rp - ((size_t)rb + 2);
    while (dn && isspace((unsigned char)*d)) { d++; dn--; }

    const char *url, *rest;
    size_t ul, rl;
    if (dn && *d == '<') {
        size_t k = 1;
        while (k < dn && d[k] != '>') k++;
        url = d + 1; ul = k - 1;
        rest = d + (k < dn ? k + 1 : dn);
        rl = dn - (size_t)(rest - d);
    } else {
        size_t k = 0;
        while (k < dn && !isspace((unsigned char)d[k])) k++;
        url = d; ul = k; rest = d + k; rl = dn - k;
    }
    while (rl && isspace((unsigned char)*rest)) { rest++; rl--; }
    while (rl && isspace((unsigned char)rest[rl - 1])) rl--;
    const char *title = NULL;
    size_t tl = 0;
    if (rl >= 2 && ((rest[0] == '"' && rest[rl - 1] == '"') || (rest[0] == '\'' && rest[rl - 1] == '\''))) {
        title = rest + 1; tl = rl - 2;
    }

    const char *txt = s + lb + 1;
    size_t txl = (size_t)rb - (lb + 1);

    if (image) {
        buf_puts(o, "<img src=\"");
        emit_url(o, url, ul, 1);
        buf_puts(o, "\" alt=\"");
        esc_n(o, txt, txl);
        buf_putc(o, '"');
        if (title) { buf_puts(o, " title=\""); esc_n(o, title, tl); buf_putc(o, '"'); }
        buf_puts(o, " loading=\"lazy\">");
    } else {
        buf_puts(o, "<a href=\"");
        emit_url(o, url, ul, 0);
        buf_putc(o, '"');
        if (title) { buf_puts(o, " title=\""); esc_n(o, title, tl); buf_putc(o, '"'); }
        buf_putc(o, '>');
        render_inline(o, txt, txl, 1);
        buf_puts(o, "</a>");
    }
    *next = (size_t)rp + 1;
    return 1;
}

static size_t scheme_len(const char *s, size_t n) {
    if (n >= 8 && strncasecmp(s, "https://", 8) == 0) return 8;
    if (n >= 7 && strncasecmp(s, "http://", 7) == 0)  return 7;
    return 0;
}

static void render_inline_core(Buf *o, const char *s, size_t n, int in_link) {
    size_t i = 0;
    while (i < n) {
        char c = s[i];
        switch (c) {

        case '\\':
            if (i + 1 < n && s[i + 1] == '\n') { buf_puts(o, "<br>\n"); i += 2; }
            else if (i + 1 < n && ispunct((unsigned char)s[i + 1])) { esc_n(o, s + i + 1, 1); i += 2; }
            else { buf_putc(o, '\\'); i++; }
            break;

        case '`': {
            size_t r = 0;
            while (i + r < n && s[i + r] == '`') r++;
            long cl = find_code_close(s, n, i + r, r);
            if (cl < 0) { buf_putn(o, s + i, r); i += r; break; }
            const char *a = s + i + r;
            size_t len = (size_t)cl - (i + r);
            if (len >= 2 && a[0] == ' ' && a[len - 1] == ' ') { a++; len -= 2; }
            buf_puts(o, "<code>");
            for (size_t k = 0; k < len; k++) {
                if (a[k] == '\n') buf_putc(o, ' ');
                else esc_n(o, a + k, 1);
            }
            buf_puts(o, "</code>");
            i = (size_t)cl + r;
            break;
        }

        case '!': {
            size_t nx;
            if (i + 1 < n && s[i + 1] == '[' && try_link(o, s, n, i + 1, 1, &nx)) { i = nx; break; }
            buf_putc(o, '!');
            i++;
            break;
        }

        case '[': {
            size_t nx;
            if (!in_link && try_link(o, s, n, i, 0, &nx)) { i = nx; break; }
            buf_putc(o, '[');
            i++;
            break;
        }

        case '*': case '_': case '~': {
            size_t r = 0;
            while (i + r < n && s[i + r] == c) r++;
            int ok = 1;
            if (c == '~' && r != 2) ok = 0;
            if (r > 3) ok = 0;
            if (i + r >= n || isspace((unsigned char)s[i + r])) ok = 0;
            if (c == '_' && i > 0 && isalnum((unsigned char)s[i - 1])) ok = 0;
            long cl = ok ? find_closer(s, n, i + r, c, r) : -1;
            if (cl < 0) { buf_putn(o, s + i, r); i += r; break; }
            const char *ot, *ct;
            if (r == 3)           { ot = "<em><strong>"; ct = "</strong></em>"; }
            else if (r == 2)      { ot = c == '~' ? "<del>" : "<strong>"; ct = c == '~' ? "</del>" : "</strong>"; }
            else                  { ot = "<em>"; ct = "</em>"; }
            buf_puts(o, ot);
            render_inline(o, s + i + r, (size_t)cl - (i + r), in_link);
            buf_puts(o, ct);
            i = (size_t)cl + r;
            break;
        }

        case '<': {
            size_t k = i + 1;
            while (k < n && s[k] != '>' && s[k] != '<' && !isspace((unsigned char)s[k])) k++;
            if (k < n && s[k] == '>' && !in_link &&
                (scheme_len(s + i + 1, n - i - 1) || (k - i > 8 && strncasecmp(s + i + 1, "mailto:", 7) == 0))) {
                buf_puts(o, "<a href=\"");
                emit_url(o, s + i + 1, k - i - 1, 0);
                buf_puts(o, "\">");
                esc_n(o, s + i + 1, k - i - 1);
                buf_puts(o, "</a>");
                i = k + 1;
            } else {
                buf_puts(o, "&lt;");
                i++;
            }
            break;
        }

        case '&': {
            size_t k = i + 1;
            while (k < n && k - i < 10 && (isalnum((unsigned char)s[k]) || s[k] == '#')) k++;
            if (k < n && s[k] == ';' && k > i + 1) { buf_putn(o, s + i, k - i + 1); i = k + 1; }
            else { buf_puts(o, "&amp;"); i++; }
            break;
        }

        case ' ': {
            size_t r = 0;
            while (i + r < n && s[i + r] == ' ') r++;
            if (r >= 2 && i + r < n && s[i + r] == '\n') buf_puts(o, "<br>");
            else buf_putn(o, s + i, r);
            i += r;
            break;
        }

        case 'h': case 'H': {
            size_t pl = scheme_len(s + i, n - i);
            if (pl && !in_link && (i == 0 || !isalnum((unsigned char)s[i - 1]))) {
                size_t k = i + pl;
                while (k < n && !isspace((unsigned char)s[k]) && s[k] != '<') k++;
                while (k > i + pl && strchr(".,;:!?)'\"", s[k - 1])) k--;
                if (k > i + pl) {
                    buf_puts(o, "<a href=\"");
                    emit_url(o, s + i, k - i, 0);
                    buf_puts(o, "\">");
                    esc_n(o, s + i, k - i);
                    buf_puts(o, "</a>");
                    i = k;
                    break;
                }
            }
            buf_putc(o, c);
            i++;
            break;
        }

        default:
            esc_n(o, s + i, 1);
            i++;
        }
    }
}

static void render_inline(Buf *o, const char *s, size_t n, int in_link) {
    static int depth;
    if (depth >= MAX_DEPTH) { esc_n(o, s, n); return; }
    depth++;
    render_inline_core(o, s, n, in_link);
    depth--;
}

typedef struct { char **v; int n, cap; } Lines;

static void lines_add(Lines *L, const char *s, size_t len) {
    if (L->n == L->cap) {
        L->cap = L->cap ? L->cap * 2 : 16;
        L->v = xrealloc(L->v, (size_t)L->cap * sizeof *L->v);
    }
    L->v[L->n++] = xstrndup(s, len);
}

static void lines_free(Lines *L) {
    for (int i = 0; i < L->n; i++) free(L->v[i]);
    free(L->v);
    L->v = NULL; L->n = L->cap = 0;
}

static Lines split_lines(const char *t) {
    Lines L = {0};
    const char *p = t;
    while (*p) {
        const char *e = strchr(p, '\n');
        size_t len = e ? (size_t)(e - p) : strlen(p);
        size_t l2 = len;
        if (l2 && p[l2 - 1] == '\r') l2--;
        lines_add(&L, p, l2);
        if (!e) break;
        p = e + 1;
    }
    return L;
}

static int indent_of(const char *l) {
    int c = 0;
    for (; *l == ' ' || *l == '\t'; l++) c = (*l == '\t') ? (c / 4 + 1) * 4 : c + 1;
    return c;
}

static const char *strip_cols(const char *l, int cols) {
    int c = 0;
    while (c < cols && (*l == ' ' || *l == '\t')) {
        c = (*l == '\t') ? (c / 4 + 1) * 4 : c + 1;
        l++;
    }
    return l;
}

static const char *skip_ws(const char *l) { while (*l == ' ' || *l == '\t') l++; return l; }

static int is_blank(const char *l) {
    for (; *l; l++) if (!isspace((unsigned char)*l)) return 0;
    return 1;
}

static int fence_open(const char *l, char *ch, int *len, const char **info) {
    if (indent_of(l) > 3) return 0;
    const char *p = skip_ws(l);
    char c = *p;
    if (c != '`' && c != '~') return 0;
    int n = 0;
    while (p[n] == c) n++;
    if (n < 3) return 0;
    if (c == '`' && strchr(p + n, '`')) return 0;
    *ch = c; *len = n; *info = p + n;
    return 1;
}

static int fence_close(const char *l, char ch, int len) {
    if (indent_of(l) > 3) return 0;
    const char *p = skip_ws(l);
    int n = 0;
    while (p[n] == ch) n++;
    return n >= len && is_blank(p + n);
}

static int heading_level(const char *l, const char **text) {
    if (indent_of(l) > 3) return 0;
    const char *p = skip_ws(l);
    int n = 0;
    while (p[n] == '#') n++;
    if (n < 1 || n > 6) return 0;
    if (p[n] && p[n] != ' ' && p[n] != '\t') return 0;
    *text = p + n;
    return n;
}

static int is_hr(const char *l) {
    if (indent_of(l) > 3) return 0;
    const char *p = skip_ws(l);
    char c = *p;
    if (c != '-' && c != '*' && c != '_') return 0;
    int n = 0;
    for (; *p; p++) {
        if (*p == c) n++;
        else if (*p != ' ' && *p != '\t') return 0;
    }
    return n >= 3;
}

static int is_quote(const char *l) { return indent_of(l) <= 3 && *skip_ws(l) == '>'; }

static int list_marker(const char *l, int *ordered, int *num, int *mw, const char **content) {
    int ind = indent_of(l);
    if (ind > 3) return 0;
    const char *p = skip_ws(l);
    int mlen;
    if ((*p == '-' || *p == '*' || *p == '+') && (p[1] == ' ' || p[1] == '\t' || p[1] == 0)) {
        *ordered = 0; *num = 0; mlen = 1;
    } else if (isdigit((unsigned char)*p)) {
        int d = 0;
        long v = 0;
        while (isdigit((unsigned char)p[d]) && d < 9) { v = v * 10 + (p[d] - '0'); d++; }
        if ((p[d] != '.' && p[d] != ')') || !(p[d + 1] == ' ' || p[d + 1] == '\t' || p[d + 1] == 0)) return 0;
        *ordered = 1; *num = (int)v; mlen = d + 1;
    } else {
        return 0;
    }
    const char *q = p + mlen;
    int sp = 0;
    while (*q == ' ') { sp++; q++; }
    if (*q == 0) { sp = 1; *content = q; }
    else if (sp >= 5) { sp = 1; *content = p + mlen + 1; }
    else *content = q;
    *mw = ind + mlen + sp;
    return 1;
}

static int starts_block(const char *l) {
    const char *t, *info, *c;
    char ch;
    int len, o, n, mw;
    if (heading_level(l, &t) || fence_open(l, &ch, &len, &info) || is_hr(l) || is_quote(l)) return 1;
    if (list_marker(l, &o, &n, &mw, &c) && (!o || n == 1) && !is_blank(c)) return 1;
    return 0;
}

static int split_cells(const char *line, const char **cs, size_t *cl, int max) {
    const char *p = skip_ws(line);
    const char *end = p + strlen(p);
    while (end > p && isspace((unsigned char)end[-1])) end--;
    if (p < end && *p == '|') p++;
    if (end > p && end[-1] == '|' && !(end - 1 > p && end[-2] == '\\')) end--;
    int nc = 0;
    const char *start = p;
    for (const char *q = p;; q++) {
        if (q == end || (*q == '|' && (q == p || q[-1] != '\\'))) {
            if (nc < max) {
                const char *a = start, *b = q;
                while (a < b && isspace((unsigned char)*a)) a++;
                while (b > a && isspace((unsigned char)b[-1])) b--;
                cs[nc] = a;
                cl[nc] = (size_t)(b - a);
                nc++;
            }
            if (q == end) break;
            start = q + 1;
        }
    }
    return nc;
}

static int delim_cell(const char *c, size_t n) {
    if (!n) return 0;
    int left = c[0] == ':', right = c[n - 1] == ':';
    size_t a = left ? 1 : 0, b = right && n > a ? n - 1 : n;
    if (b <= a) return 0;
    for (size_t i = a; i < b; i++) if (c[i] != '-') return 0;
    return left && right ? 3 : left ? 2 : right ? 4 : 1;
}

static void put_align(Buf *o, int a) {
    if (a == 2) buf_puts(o, " style=\"text-align:left\"");
    else if (a == 3) buf_puts(o, " style=\"text-align:center\"");
    else if (a == 4) buf_puts(o, " style=\"text-align:right\"");
}

static void render_blocks(Buf *o, Lines *L, int tight, int depth);

static void render_blocks(Buf *o, Lines *L, int tight, int depth) {
    int n = L->n;
    if (depth > MAX_DEPTH) return;

    for (int i = 0; i < n;) {
        const char *l = L->v[i];
        if (is_blank(l)) { i++; continue; }

        char fch;
        int flen;
        const char *info;
        if (fence_open(l, &fch, &flen, &info)) {
            int find = indent_of(l);
            info = skip_ws(info);
            size_t ll = 0;
            while (info[ll] && !isspace((unsigned char)info[ll])) ll++;
            if (ll) {
                buf_puts(o, "<pre><code class=\"language-");
                esc_n(o, info, ll);
                buf_puts(o, "\">");
            } else {
                buf_puts(o, "<pre><code>");
            }
            int j = i + 1;
            for (; j < n && !fence_close(L->v[j], fch, flen); j++) {
                const char *code = strip_cols(L->v[j], find);
                esc_n(o, code, strlen(code));
                buf_putc(o, '\n');
            }
            buf_puts(o, "</code></pre>\n");
            i = j < n ? j + 1 : j;
            continue;
        }

        if (strncmp(skip_ws(l), "<!--", 4) == 0) {
            int j = i;
            while (j < n && !strstr(L->v[j], "-->")) j++;
            i = j < n ? j + 1 : j;
            continue;
        }

        const char *ht;
        int lv = heading_level(l, &ht);
        if (lv) {
            const char *t = skip_ws(ht);
            size_t tl = strlen(t);
            while (tl && isspace((unsigned char)t[tl - 1])) tl--;
            size_t e = tl;
            while (e && t[e - 1] == '#') e--;
            if (e < tl && (e == 0 || t[e - 1] == ' ' || t[e - 1] == '\t')) {
                tl = e;
                while (tl && isspace((unsigned char)t[tl - 1])) tl--;
            }
            buf_printf(o, "<h%d id=\"", lv);
            for (size_t k = 0; k < tl; k++) {
                unsigned char ch = (unsigned char)t[k];
                if (isalnum(ch) || ch >= 0x80) buf_putc(o, (char)tolower(ch));
                else if (ch == ' ' || ch == '-') buf_putc(o, '-');
                else if (ch == '_') buf_putc(o, '_');
            }
            buf_puts(o, "\">");
            render_inline(o, t, tl, 0);
            buf_printf(o, "</h%d>\n", lv);
            if (lv == 1 && !g_title[0]) {
                size_t w = 0;
                for (size_t k = 0; k < tl && w < sizeof g_title - 1; k++)
                    if (t[k] != '`' && t[k] != '*') g_title[w++] = t[k];
                g_title[w] = 0;
            }
            i++;
            continue;
        }

        if (is_hr(l)) { buf_puts(o, "<hr>\n"); i++; continue; }

        if (is_quote(l)) {
            Lines sub = {0};
            int j = i;
            while (j < n && is_quote(L->v[j])) {
                const char *p = skip_ws(L->v[j]) + 1;
                if (*p == ' ') p++;
                lines_add(&sub, p, strlen(p));
                j++;
            }
            buf_puts(o, "<blockquote>\n");
            render_blocks(o, &sub, 0, depth + 1);
            buf_puts(o, "</blockquote>\n");
            lines_free(&sub);
            i = j;
            continue;
        }

        int ordered, num, mw;
        const char *content;
        if (list_marker(l, &ordered, &num, &mw, &content)) {
            Lines *items = NULL;
            int ni = 0, icap = 0, loose = 0, start = num;
            int j = i;

            while (j < n) {
                int o2, n2, m2;
                const char *c2;
                const char *m = L->v[j];
                if (is_hr(m) || !list_marker(m, &o2, &n2, &m2, &c2) || o2 != ordered) break;

                Lines it = {0};
                lines_add(&it, c2, strlen(c2));
                int in_fence = 0;
                char ich = 0;
                int ilen = 0;
                const char *iinfo;
                if (fence_open(it.v[0], &ich, &ilen, &iinfo)) in_fence = 1;

                int k = j + 1;
                while (k < n) {
                    const char *x = L->v[k];
                    if (is_blank(x)) {
                        int q = k;
                        while (q < n && is_blank(L->v[q])) q++;
                        if (q < n && indent_of(L->v[q]) >= m2) {
                            for (; k < q; k++) lines_add(&it, "", 0);
                            continue;
                        }
                        break;
                    }
                    const char *y = NULL;
                    if (indent_of(x) >= m2) {
                        y = strip_cols(x, m2);
                    } else {
                        int o3, n3, m3;
                        const char *c3;
                        const char *prev = it.v[it.n - 1];
                        const char *pw = skip_ws(prev);
                        if (!in_fence && !is_blank(prev) &&
                            strncmp(pw, "```", 3) != 0 && strncmp(pw, "~~~", 3) != 0 &&
                            !starts_block(x) && !list_marker(x, &o3, &n3, &m3, &c3))
                            y = skip_ws(x);
                    }
                    if (!y) break;
                    if (in_fence) { if (fence_close(y, ich, ilen)) in_fence = 0; }
                    else if (fence_open(y, &ich, &ilen, &iinfo)) in_fence = 1;
                    lines_add(&it, y, strlen(y));
                    k++;
                }

                if (ni == icap) {
                    icap = icap ? icap * 2 : 8;
                    items = xrealloc(items, (size_t)icap * sizeof *items);
                }
                items[ni++] = it;
                j = k;

                int q = j;
                while (q < n && is_blank(L->v[q])) q++;
                if (q > j) {
                    int o3, n3, m3;
                    const char *c3;
                    if (q < n && !is_hr(L->v[q]) && list_marker(L->v[q], &o3, &n3, &m3, &c3) && o3 == ordered) {
                        loose = 1;
                        j = q;
                    } else {
                        break;
                    }
                }
            }

            if (ordered && start != 1) buf_printf(o, "<ol start=\"%d\">\n", start);
            else buf_puts(o, ordered ? "<ol>\n" : "<ul>\n");

            for (int k = 0; k < ni; k++) {
                Lines *it = &items[k];
                buf_puts(o, "<li>");
                char *f = it->v[0];
                if (f[0] == '[' && (f[1] == ' ' || f[1] == 'x' || f[1] == 'X') && f[2] == ']' && (f[3] == ' ' || f[3] == 0)) {
                    buf_printf(o, "<input type=\"checkbox\" disabled%s> ", f[1] == ' ' ? "" : " checked");
                    size_t skip = f[3] ? 4 : 3;
                    memmove(f, f + skip, strlen(f + skip) + 1);
                }
                render_blocks(o, it, !loose, depth + 1);
                buf_puts(o, "</li>\n");
                lines_free(it);
            }
            free(items);
            buf_puts(o, ordered ? "</ol>\n" : "</ul>\n");
            i = j;
            continue;
        }

        if (strchr(l, '|') && i + 1 < n && strchr(L->v[i + 1], '-')) {
            const char *hc[MAXC], *dc[MAXC];
            size_t hl[MAXC], dl[MAXC];
            int nh = split_cells(l, hc, hl, MAXC);
            int nd = split_cells(L->v[i + 1], dc, dl, MAXC);
            int align[MAXC], ok = (nh == nd && nh >= 1);
            for (int c = 0; ok && c < nd; c++) {
                align[c] = delim_cell(dc[c], dl[c]);
                if (!align[c]) ok = 0;
            }
            if (ok) {
                buf_puts(o, "<table>\n<thead>\n<tr>");
                for (int c = 0; c < nh; c++) {
                    buf_puts(o, "<th");
                    put_align(o, align[c]);
                    buf_putc(o, '>');
                    render_inline(o, hc[c], hl[c], 0);
                    buf_puts(o, "</th>");
                }
                buf_puts(o, "</tr>\n</thead>\n<tbody>\n");
                int j = i + 2;
                while (j < n && !is_blank(L->v[j]) && strchr(L->v[j], '|')) {
                    const char *rc[MAXC];
                    size_t rl[MAXC];
                    int nr = split_cells(L->v[j], rc, rl, MAXC);
                    buf_puts(o, "<tr>");
                    for (int c = 0; c < nh; c++) {
                        buf_puts(o, "<td");
                        put_align(o, align[c]);
                        buf_putc(o, '>');
                        if (c < nr) render_inline(o, rc[c], rl[c], 0);
                        buf_puts(o, "</td>");
                    }
                    buf_puts(o, "</tr>\n");
                    j++;
                }
                buf_puts(o, "</tbody>\n</table>\n");
                i = j;
                continue;
            }
        }

        Buf pb = {0};
        int j = i;
        while (j < n && !is_blank(L->v[j]) && (j == i || !starts_block(L->v[j]))) {
            if (j > i) buf_putc(&pb, '\n');
            buf_puts(&pb, skip_ws(L->v[j]));
            j++;
        }
        while (pb.len && isspace((unsigned char)pb.d[pb.len - 1])) pb.len--;
        if (!tight) buf_puts(o, "<p>");
        render_inline(o, pb.d ? pb.d : "", pb.len, 0);
        if (!tight) buf_puts(o, "</p>");
        buf_putc(o, '\n');
        free(pb.d);
        i = j;
    }
}

void md_render(const char *text, Buf *out, char *title, size_t title_sz) {
    g_title[0] = 0;
    if (strncmp(text, "\xEF\xBB\xBF", 3) == 0) text += 3;   
    Lines L = split_lines(text);
    render_blocks(out, &L, 0, 0);
    lines_free(&L);
    if (title && title_sz) snprintf(title, title_sz, "%s", g_title);
}
