#include "buf.h"

#include <ctype.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void *xrealloc(void *p, size_t n) {
    p = realloc(p, n);
    if (!p) { perror("realloc"); exit(1); }
    return p;
}

char *xstrndup(const char *s, size_t n) {
    char *p = xrealloc(NULL, n + 1);
    memcpy(p, s, n);
    p[n] = 0;
    return p;
}

void buf_reserve(Buf *b, size_t extra) {
    if (b->d && b->len + extra + 1 <= b->cap) return;
    size_t cap = b->cap ? b->cap : 256;
    while (cap < b->len + extra + 1) cap *= 2;
    b->d = xrealloc(b->d, cap);
    b->cap = cap;
    b->d[b->len] = 0;
}

void buf_putn(Buf *b, const char *s, size_t n) {
    buf_reserve(b, n);
    memcpy(b->d + b->len, s, n);
    b->len += n;
    b->d[b->len] = 0;
}
void buf_puts(Buf *b, const char *s) { buf_putn(b, s, strlen(s)); }
void buf_putc(Buf *b, char c)        { buf_putn(b, &c, 1); }

void buf_printf(Buf *b, const char *fmt, ...) {
    va_list ap, ap2;
    va_start(ap, fmt);
    va_copy(ap2, ap);
    int n = vsnprintf(NULL, 0, fmt, ap);
    va_end(ap);
    if (n > 0) {
        buf_reserve(b, (size_t)n);
        vsnprintf(b->d + b->len, (size_t)n + 1, fmt, ap2);
        b->len += (size_t)n;
    }
    va_end(ap2);
}

void esc_n(Buf *b, const char *s, size_t n) {
    for (size_t i = 0; i < n; i++) {
        switch (s[i]) {
        case '&': buf_puts(b, "&amp;");  break;
        case '<': buf_puts(b, "&lt;");   break;
        case '>': buf_puts(b, "&gt;");   break;
        case '"': buf_puts(b, "&quot;"); break;
        default:  buf_putc(b, s[i]);
        }
    }
}
void esc(Buf *b, const char *s) { esc_n(b, s, strlen(s)); }

void url_enc(Buf *o, const char *s) {
    for (; *s; s++) {
        unsigned char c = (unsigned char)*s;
        if (isalnum(c) || strchr("-._~/", c)) buf_putc(o, (char)c);
        else buf_printf(o, "%%%02X", c);
    }
}
