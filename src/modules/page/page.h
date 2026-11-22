#ifndef PAGE_H
#define PAGE_H

#include "../buffer/buf.h"

void page_begin(Buf *o, const char *title, int user_css, const char *hljs_base);
void page_end(Buf *o, const char *hljs_base);

const char *page_css(void);

#endif
