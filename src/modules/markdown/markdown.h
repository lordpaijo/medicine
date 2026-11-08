#ifndef MARKDOWN_H
#define MARKDOWN_H

#include <stddef.h>
#include "../buffer/buf.h"

void md_render(const char *text, Buf *out, char *title, size_t title_sz);

#endif
