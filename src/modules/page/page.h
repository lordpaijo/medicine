#ifndef PAGE_H
#define PAGE_H

#include "../buffer/buf.h"

void page_begin(Buf *o, const char *title, int user_css, int highlight);
void page_end(Buf *o, int highlight);

const char *page_css(void);

#endif
