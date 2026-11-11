#include "http.h"

#include <stdio.h>
#include <string.h>

#include "../buffer/buf.h"
#include "../platform/platform.h"

static PLAT_TLS int g_last_status;

int  http_last_status(void)  { return g_last_status; }
void http_reset_status(void) { g_last_status = 0; }

const char *reason_of(int code) {
    switch (code) {
    case 200: return "OK";
    case 301: return "Moved Permanently";
    case 400: return "Bad Request";
    case 403: return "Forbidden";
    case 404: return "Not Found";
    case 405: return "Method Not Allowed";
    case 413: return "Payload Too Large";
    default:  return "Internal Server Error";
    }
}

void send_all(sock_t fd, const char *p, size_t n) {
    while (n) {
        int w = plat_send(fd, p, n);
        if (w <= 0) return;
        p += w;
        n -= (size_t)w;
    }
}

static void u64_str(char *out, unsigned long long v) {
    char t[24];
    int n = 0, i = 0;
    do { t[n++] = (char)('0' + v % 10); v /= 10; } while (v);
    while (n) out[i++] = t[--n];
    out[i] = 0;
}

void send_headers(sock_t fd, int code, const char *ctype, unsigned long long len, const char *extra) {
    char h[1024], lens[24];
    u64_str(lens, len);
    int n = snprintf(h, sizeof h,
        "HTTP/1.1 %d %s\r\nServer: mdserve\r\nContent-Type: %s\r\n"
        "Content-Length: %s\r\nX-Content-Type-Options: nosniff\r\n"
        "Connection: close\r\n%s\r\n",
        code, reason_of(code), ctype, lens, extra ? extra : "");
    g_last_status = code;
    if (n > 0) send_all(fd, h, (size_t)(n < (int)sizeof h ? n : (int)sizeof h - 1));
}

void send_body(sock_t fd, int code, const char *ctype, const char *extra,
               const char *body, size_t len, int head) {
    send_headers(fd, code, ctype, len, extra);
    if (!head) send_all(fd, body, len);
}

static const struct { const char *ext, *type; } MIME[] = {
    {"html", "text/html; charset=utf-8"},  {"htm",  "text/html; charset=utf-8"},
    {"css",  "text/css; charset=utf-8"},   {"js",   "text/javascript; charset=utf-8"},
    {"mjs",  "text/javascript; charset=utf-8"}, {"json", "application/json"},
    {"txt",  "text/plain; charset=utf-8"}, {"md",   "text/plain; charset=utf-8"},
    {"c",    "text/plain; charset=utf-8"}, {"h",    "text/plain; charset=utf-8"},
    {"sh",   "text/plain; charset=utf-8"}, {"rs",   "text/plain; charset=utf-8"},
    {"py",   "text/plain; charset=utf-8"}, {"toml", "text/plain; charset=utf-8"},
    {"yml",  "text/plain; charset=utf-8"}, {"yaml", "text/plain; charset=utf-8"},
    {"log",  "text/plain; charset=utf-8"}, {"csv",  "text/csv; charset=utf-8"},
    {"xml",  "application/xml"},           {"pdf",  "application/pdf"},
    {"png",  "image/png"},                 {"jpg",  "image/jpeg"},
    {"jpeg", "image/jpeg"},                {"gif",  "image/gif"},
    {"webp", "image/webp"},                {"svg",  "image/svg+xml"},
    {"ico",  "image/x-icon"},              {"woff", "font/woff"},
    {"woff2","font/woff2"},                {"ttf",  "font/ttf"},
    {"mp3",  "audio/mpeg"},                {"mp4",  "video/mp4"},
    {"webm", "video/webm"},                {"wasm", "application/wasm"},
    {NULL, NULL}
};

const char *mime_of(const char *path) {
    const char *dot = strrchr(path, '.');
    if (dot) {
        for (int i = 0; MIME[i].ext; i++)
            if (ci_cmp(dot + 1, MIME[i].ext) == 0) return MIME[i].type;
    }
    return "application/octet-stream";
}
