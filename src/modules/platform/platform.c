/* platform.c - the Unix and Windows implementations of platform.h.
 *
 * Layout of this file:
 *   1. shared helpers (pure C, same on every OS)
 *   2. #ifdef _WIN32  ... Windows implementation ...
 *   3. #else          ... Unix implementation ...
 */
#define _GNU_SOURCE
#include "platform.h"

#include <ctype.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>

#define IO_TIMEOUT_MS 10000

static int ieq_n(const char *a, const char *b, size_t n) {
    for (size_t i = 0; i < n; i++)
        if (tolower((unsigned char)a[i]) != tolower((unsigned char)b[i])) return 0;
    return 1;
}

int plat_path_unsafe_win(const char *p) {
    if (strchr(p, '\\') || strchr(p, ':')) return 1;

    for (const char *c = p; *c; c++)
        if ((unsigned char)*c < 0x20 || strchr("*?<>\"|", *c)) return 1;

    const char *s = p;
    while (*s) {
        while (*s == '/') s++;
        if (!*s) break;
        const char *e = s;
        while (*e && *e != '/') e++;
        size_t len = (size_t)(e - s);

        if (s[len - 1] == '.' || s[len - 1] == ' ') return 1;

        for (size_t i = 0; i + 1 < len; i++)
            if (s[i] == '~' && isdigit((unsigned char)s[i + 1])) return 1;

        size_t b = 0;
        while (b < len && s[b] != '.') b++;
        while (b > 0 && s[b - 1] == ' ') b--;
        if (b == 3 && (ieq_n(s, "CON", 3) || ieq_n(s, "PRN", 3) ||
                       ieq_n(s, "AUX", 3) || ieq_n(s, "NUL", 3)))
            return 1;
        if (b == 4 && (ieq_n(s, "COM", 3) || ieq_n(s, "LPT", 3)) && s[3] >= '1' && s[3] <= '9')
            return 1;

        s = e;
    }
    return 0;
}

#ifdef _WIN32

#include <windows.h>
#include <shellapi.h>

static wchar_t *utf8_to_wide(const char *s) {
    int n = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, s, -1, NULL, 0);
    if (n <= 0) return NULL;
    wchar_t *w = malloc((size_t)n * sizeof *w);
    if (!w) return NULL;
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, s, -1, w, n);
    return w;
}

static char *wide_to_utf8(const wchar_t *w) {
    int n = WideCharToMultiByte(CP_UTF8, 0, w, -1, NULL, 0, NULL, NULL);
    if (n <= 0) return NULL;
    char *s = malloc((size_t)n);
    if (!s) return NULL;
    WideCharToMultiByte(CP_UTF8, 0, w, -1, s, n, NULL, NULL);
    return s;
}

void plat_args(int *argc, char ***argv) {
    int wn = 0;
    wchar_t **wa = CommandLineToArgvW(GetCommandLineW(), &wn);
    if (!wa) return;
    char **na = calloc((size_t)wn + 1, sizeof *na);
    int ok = na != NULL;
    for (int i = 0; ok && i < wn; i++) {
        na[i] = wide_to_utf8(wa[i]);
        if (!na[i]) ok = 0;
    }
    LocalFree(wa);
    if (!ok) {
        if (na) { for (int i = 0; i < wn; i++) free(na[i]); free(na); }
        return;
    }
    *argc = wn;
    *argv = na;
}

int plat_init(void) {
    WSADATA wsa;
    int r = WSAStartup(MAKEWORD(2, 2), &wsa);
    if (r != 0) { fprintf(stderr, "WSAStartup failed (error %d)\n", r); return -1; }
    SetConsoleOutputCP(CP_UTF8);
    return 0;
}

static void set_timeouts(sock_t s, int ms) {
    DWORD t = (DWORD)ms;
    setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, (const char *)&t, sizeof t);
    setsockopt(s, SOL_SOCKET, SO_SNDTIMEO, (const char *)&t, sizeof t);
}

sock_t plat_listen(const char *addr, int port) {
    sock_t s = socket(AF_INET, SOCK_STREAM, 0);
    if (s == INVALID_SOCKET) { fprintf(stderr, "socket: error %d\n", WSAGetLastError()); return SOCK_BAD; }

    int one = 1;
    setsockopt(s, SOL_SOCKET, SO_EXCLUSIVEADDRUSE, (const char *)&one, sizeof one);

    struct sockaddr_in a;
    memset(&a, 0, sizeof a);
    a.sin_family = AF_INET;
    a.sin_port = htons((unsigned short)port);
    if (inet_pton(AF_INET, addr, &a.sin_addr) != 1) {
        fprintf(stderr, "bad address: %s\n", addr);
        closesocket(s);
        return SOCK_BAD;
    }
    if (bind(s, (struct sockaddr *)&a, sizeof a) == SOCKET_ERROR) {
        int e = WSAGetLastError();
        fprintf(stderr, "bind: error %d%s\n", e, e == WSAEADDRINUSE ? " (address already in use)" : "");
        closesocket(s);
        return SOCK_BAD;
    }
    if (listen(s, 64) == SOCKET_ERROR) {
        fprintf(stderr, "listen: error %d\n", WSAGetLastError());
        closesocket(s);
        return SOCK_BAD;
    }
    return s;
}

#define MAX_CONN 64

typedef struct {
    SOCKET s;
    char ip[INET_ADDRSTRLEN];
    void (*handler)(sock_t, const char *);
} Conn;

static volatile LONG g_active;

static DWORD WINAPI conn_thread(LPVOID arg) {
    Conn c = *(Conn *)arg;
    free(arg);
    set_timeouts(c.s, IO_TIMEOUT_MS);
    c.handler(c.s, c.ip);
    closesocket(c.s);
    InterlockedDecrement(&g_active);
    return 0;
}

void plat_serve(sock_t srv, void (*handler)(sock_t, const char *)) {
    for (;;) {
        struct sockaddr_in ca;
        int cl = (int)sizeof ca;
        SOCKET c = accept(srv, (struct sockaddr *)&ca, &cl);
        if (c == INVALID_SOCKET) {
            fprintf(stderr, "accept: error %d\n", WSAGetLastError());
            Sleep(50);
            continue;
        }
        if (InterlockedIncrement(&g_active) > MAX_CONN) {
            InterlockedDecrement(&g_active);
            closesocket(c);
            continue;
        }
        Conn *cn = malloc(sizeof *cn);
        if (!cn) { InterlockedDecrement(&g_active); closesocket(c); continue; }
        cn->s = c;
        cn->handler = handler;
        inet_ntop(AF_INET, &ca.sin_addr, cn->ip, sizeof cn->ip);
        HANDLE h = CreateThread(NULL, 0, conn_thread, cn, 0, NULL);
        if (!h) { InterlockedDecrement(&g_active); closesocket(c); free(cn); continue; }
        CloseHandle(h);
    }
}

int plat_recv(sock_t s, char *buf, size_t n) {
    int r = recv(s, buf, (int)n, 0);
    return r == SOCKET_ERROR ? -1 : r;
}

int plat_send(sock_t s, const char *buf, size_t n) {
    int r = send(s, buf, (int)n, 0);
    return r == SOCKET_ERROR ? -1 : r;
}

int plat_stat(const char *path, PlatStat *st) {
    wchar_t *w = utf8_to_wide(path);
    if (!w) return -1;
    WIN32_FILE_ATTRIBUTE_DATA d;
    BOOL ok = GetFileAttributesExW(w, GetFileExInfoStandard, &d);
    free(w);
    if (!ok) return -1;
    st->is_dir  = (d.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
    st->is_file = !st->is_dir;
    st->size    = ((long long)d.nFileSizeHigh << 32) | d.nFileSizeLow;
    return 0;
}

char *plat_realpath(const char *path, char *out) {
    wchar_t *w = utf8_to_wide(path);
    if (!w) return NULL;
    HANDLE h = CreateFileW(w, 0, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                           NULL, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, NULL);
    free(w);
    if (h == INVALID_HANDLE_VALUE) return NULL;

    DWORD need = GetFinalPathNameByHandleW(h, NULL, 0, VOLUME_NAME_DOS);
    if (need == 0) { CloseHandle(h); return NULL; }
    wchar_t *fin = malloc(((size_t)need + 1) * sizeof *fin);
    if (!fin) { CloseHandle(h); return NULL; }
    DWORD got = GetFinalPathNameByHandleW(h, fin, need + 1, VOLUME_NAME_DOS);
    CloseHandle(h);
    if (got == 0 || got > need) { free(fin); return NULL; }

    const wchar_t *p = fin;
    if (wcsncmp(p, L"\\\\?\\UNC\\", 8) == 0) { fin[6] = L'\\'; p = fin + 6; }
    else if (wcsncmp(p, L"\\\\?\\", 4) == 0) p += 4;

    char *u = wide_to_utf8(p);
    free(fin);
    if (!u) return NULL;
    size_t len = strlen(u);
    if (len >= PLAT_PATH_MAX) { free(u); return NULL; }
    for (size_t i = 0; i < len; i++) if (u[i] == '\\') u[i] = '/';
    memcpy(out, u, len + 1);
    free(u);
    return out;
}

int plat_inside(const char *path, const char *root, size_t rootlen) {
    return _strnicmp(path, root, rootlen) == 0 && (path[rootlen] == '/' || path[rootlen] == 0);
}

int plat_path_unsafe(const char *urlpath) { return plat_path_unsafe_win(urlpath); }

FILE *plat_fopen_rb(const char *path) {
    wchar_t *w = utf8_to_wide(path);
    if (!w) return NULL;
    FILE *f = _wfopen(w, L"rb");
    free(w);
    return f;
}

int plat_listdir(const char *dir, PlatDirCb cb, void *user) {
    size_t n = strlen(dir);
    char *pat = malloc(n + 3);
    if (!pat) return -1;
    memcpy(pat, dir, n);
    memcpy(pat + n, "/*", 3);
    wchar_t *w = utf8_to_wide(pat);
    free(pat);
    if (!w) return -1;
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW(w, &fd);
    free(w);
    if (h == INVALID_HANDLE_VALUE) return -1;
    do {
        if (fd.cFileName[0] == L'.') continue;
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_HIDDEN) continue;
        char *u = wide_to_utf8(fd.cFileName);
        if (!u) continue;
        cb(u, (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0, user);
        free(u);
    } while (FindNextFileW(h, &fd));
    FindClose(h);
    return 0;
}

#else

#include <arpa/inet.h>
#include <dirent.h>
#include <netinet/in.h>
#include <signal.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>

#ifndef MSG_NOSIGNAL
  #define MSG_NOSIGNAL 0
#endif

void plat_args(int *argc, char ***argv) { (void)argc; (void)argv; }

int plat_init(void) {
    signal(SIGPIPE, SIG_IGN);
    signal(SIGCHLD, SIG_IGN);
    return 0;
}

static void set_timeouts(sock_t s, int ms) {
    struct timeval tv;
    tv.tv_sec = ms / 1000;
    tv.tv_usec = (ms % 1000) * 1000;
    setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);
    setsockopt(s, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof tv);
}

sock_t plat_listen(const char *addr, int port) {
    sock_t s = socket(AF_INET, SOCK_STREAM, 0);
    if (s < 0) { perror("socket"); return SOCK_BAD; }

    int one = 1;
    setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one);

    struct sockaddr_in a;
    memset(&a, 0, sizeof a);
    a.sin_family = AF_INET;
    a.sin_port = htons((unsigned short)port);
    if (inet_pton(AF_INET, addr, &a.sin_addr) != 1) {
        fprintf(stderr, "bad address: %s\n", addr);
        close(s);
        return SOCK_BAD;
    }
    if (bind(s, (struct sockaddr *)&a, sizeof a) < 0) { perror("bind"); close(s); return SOCK_BAD; }
    if (listen(s, 64) < 0) { perror("listen"); close(s); return SOCK_BAD; }
    return s;
}

void plat_serve(sock_t srv, void (*handler)(sock_t, const char *)) {
    for (;;) {
        struct sockaddr_in ca;
        socklen_t cl = sizeof ca;
        int c = accept(srv, (struct sockaddr *)&ca, &cl);
        if (c < 0) { if (errno != EINTR) perror("accept"); continue; }

        pid_t pid = fork();
        if (pid == 0) {
            close(srv);
            alarm(30);
            set_timeouts(c, IO_TIMEOUT_MS);
            char ip[INET_ADDRSTRLEN];
            inet_ntop(AF_INET, &ca.sin_addr, ip, sizeof ip);
            handler(c, ip);
            close(c);
            _exit(0);
        }
        close(c);
    }
}

int plat_recv(sock_t s, char *buf, size_t n) {
    for (;;) {
        ssize_t r = recv(s, buf, n, 0);
        if (r < 0 && errno == EINTR) continue;
        return (int)r;
    }
}

int plat_send(sock_t s, const char *buf, size_t n) {
    for (;;) {
        ssize_t r = send(s, buf, n, MSG_NOSIGNAL);
        if (r < 0 && errno == EINTR) continue;
        return (int)r;
    }
}

int plat_stat(const char *path, PlatStat *st) {
    struct stat s;
    if (stat(path, &s) < 0) return -1;
    st->is_dir  = S_ISDIR(s.st_mode) != 0;
    st->is_file = S_ISREG(s.st_mode) != 0;
    st->size    = (long long)s.st_size;
    return 0;
}

char *plat_realpath(const char *path, char *out) { return realpath(path, out); }

int plat_inside(const char *path, const char *root, size_t rootlen) {
    return strncmp(path, root, rootlen) == 0 && (path[rootlen] == '/' || path[rootlen] == 0);
}

int plat_path_unsafe(const char *urlpath) { (void)urlpath; return 0; }

FILE *plat_fopen_rb(const char *path) { return fopen(path, "rb"); }

int plat_listdir(const char *dir, PlatDirCb cb, void *user) {
    DIR *d = opendir(dir);
    if (!d) return -1;
    struct dirent *e;
    while ((e = readdir(d)) != NULL) {
        if (e->d_name[0] == '.') continue;
        char full[PLAT_PATH_MAX * 2];
        PlatStat st;
        snprintf(full, sizeof full, "%s/%s", dir, e->d_name);
        cb(e->d_name, plat_stat(full, &st) == 0 && st.is_dir, user);
    }
    closedir(d);
    return 0;
}

#endif
