#ifndef PLATFORM_H
#define PLATFORM_H

#include <stddef.h>
#include <stdio.h>

#include "compat.h"

#ifdef _WIN32
  #ifndef WIN32_LEAN_AND_MEAN
    #define WIN32_LEAN_AND_MEAN
  #endif
  #include <winsock2.h>
  #include <ws2tcpip.h>
  typedef SOCKET sock_t;
  #define SOCK_BAD INVALID_SOCKET
#else
  typedef int sock_t;
  #define SOCK_BAD (-1)
#endif

#define PLAT_PATH_MAX 4096

void plat_args(int *argc, char ***argv);
int plat_init(void);
sock_t plat_listen(const char *addr, int port);
void plat_serve(sock_t srv, void (*handler)(sock_t client, const char *ip));
int plat_recv(sock_t s, char *buf, size_t n);
int plat_send(sock_t s, const char *buf, size_t n);

typedef struct { int is_dir, is_file; long long size; } PlatStat;
int   plat_stat(const char *path, PlatStat *st);
char *plat_realpath(const char *path, char *out);
int   plat_inside(const char *path, const char *root, size_t rootlen);
int   plat_path_unsafe(const char *urlpath);
int   plat_path_unsafe_win(const char *urlpath);

FILE *plat_fopen_rb(const char *path);
typedef void (*PlatDirCb)(const char *name, int is_dir, void *user);
int   plat_listdir(const char *dir, PlatDirCb cb, void *user);

#endif
