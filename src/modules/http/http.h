#ifndef HTTP_H
#define HTTP_H

#include <stddef.h>

const char *reason_of(int code);                    /* "Not Found" for 404, ... */
const char *mime_of(const char *path);              /* Content-Type from file extension */

void send_all(int fd, const char *p, size_t n);
void send_headers(int fd, int code, const char *ctype, size_t len, const char *extra);
void send_body(int fd, int code, const char *ctype, const char *extra,
               const char *body, size_t len, int head);

int  http_last_status(void);
void http_reset_status(void);

#endif
