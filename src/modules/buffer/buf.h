#ifndef BUF_H
#define BUF_H

#include <stddef.h>

typedef struct { char *d; size_t len, cap; } Buf;

void *xrealloc(void *p, size_t n);                
char *xstrndup(const char *s, size_t n);

void buf_reserve(Buf *b, size_t extra);
void buf_putn(Buf *b, const char *s, size_t n);
void buf_puts(Buf *b, const char *s);
void buf_putc(Buf *b, char c);
void buf_printf(Buf *b, const char *fmt, ...) __attribute__((format(printf, 2, 3)));

void esc_n(Buf *b, const char *s, size_t n);       
void esc(Buf *b, const char *s);                  
void url_enc(Buf *o, const char *s);             

#endif
