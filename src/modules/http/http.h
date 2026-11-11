/* http.h - low-level HTTP response helpers and MIME lookup */
#ifndef HTTP_H
#define HTTP_H

#include <stddef.h>

#include "../platform/platform.h"

const char *reason_of(int code);
const char *mime_of(const char *path);

void send_all(sock_t fd, const char *p, size_t n);
void send_headers(sock_t fd, int code, const char *ctype, unsigned long long len, const char *extra);
void send_body(sock_t fd, int code, const char *ctype, const char *extra,
               const char *body, size_t len, int head);

int  http_last_status(void);
void http_reset_status(void);

#endif
