#ifndef SERVER_H
#define SERVER_H

int server_init(const char *docroot);
const char *server_root(void);

void server_handle(int fd, const char *ip);

#endif
