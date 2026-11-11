#ifndef SERVER_H
#define SERVER_H

#include "../platform/platform.h"

int server_init(const char *docroot);
const char *server_root(void);
void server_handle(sock_t fd, const char *ip);

#endif
