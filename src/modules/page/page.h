#ifndef PAGE_H
#define PAGE_H

#include "../buffer/buf.h"

void page_begin(Buf *o, const char *title, int user_css);
void page_end(Buf *o);

const char *page_css(void);

#endif
