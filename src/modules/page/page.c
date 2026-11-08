#include "page.h"

static const char DEFAULT_CSS[] =
    ":root{color-scheme:light dark;--bg:#fff;--fg:#1f2328;--muted:#656d76;--border:#d0d7de;"
    "--code-bg:#f6f8fa;--link:#0969da;--quote:#d0d7de}\n"
    "@media(prefers-color-scheme:dark){:root{--bg:#0d1117;--fg:#e6edf3;--muted:#8d96a0;"
    "--border:#30363d;--code-bg:#161b22;--link:#4493f8;--quote:#3d444d}}\n"
    "*{box-sizing:border-box}\n"
    "body{margin:0;background:var(--bg);color:var(--fg);"
    "font:16px/1.6 -apple-system,BlinkMacSystemFont,'Segoe UI',Helvetica,Arial,sans-serif}\n"
    ".markdown-body{max-width:860px;margin:0 auto;padding:32px 24px 64px;overflow-wrap:break-word}\n"
    ".markdown-body>:first-child{margin-top:0}\n"
    "h1,h2,h3,h4,h5,h6{margin:1.6em 0 .6em;line-height:1.25;font-weight:600}\n"
    "h1{font-size:2em;padding-bottom:.3em;border-bottom:1px solid var(--border)}\n"
    "h2{font-size:1.5em;padding-bottom:.3em;border-bottom:1px solid var(--border)}\n"
    "h3{font-size:1.25em}h4{font-size:1em}h5{font-size:.875em}h6{font-size:.85em;color:var(--muted)}\n"
    "p,ul,ol,pre,blockquote,table{margin:0 0 16px}\n"
    "a{color:var(--link);text-decoration:none}a:hover{text-decoration:underline}\n"
    "img{max-width:100%;vertical-align:middle}\n"
    "ul,ol{padding-left:2em}li+li{margin-top:.25em}li>pre,li>ul,li>ol{margin-top:8px}\n"
    "code{font:85% ui-monospace,SFMono-Regular,Menlo,Consolas,monospace;"
    "background:var(--code-bg);padding:.2em .4em;border-radius:6px}\n"
    "pre{background:var(--code-bg);padding:16px;overflow:auto;border-radius:6px;line-height:1.45}\n"
    "pre code{background:none;padding:0;font-size:85%}\n"
    "blockquote{margin-left:0;padding:0 1em;color:var(--muted);border-left:.25em solid var(--quote)}\n"
    "hr{height:.25em;border:0;background:var(--border);margin:24px 0}\n"
    "table{border-collapse:collapse;display:block;overflow:auto}\n"
    "th,td{border:1px solid var(--border);padding:6px 13px}th{font-weight:600}\n"
    "tr:nth-child(2n){background:var(--code-bg)}\n"
    "input[type=checkbox]{margin-right:.5em}\n"
    "del{color:var(--muted)}\n"
    ".dir-list{list-style:none;padding-left:0}.dir-list li{padding:2px 0}\n";

const char *page_css(void) { return DEFAULT_CSS; }

void page_begin(Buf *o, const char *title, int user_css) {
    buf_puts(o,
        "<!doctype html>\n<html lang=\"en\">\n<head>\n<meta charset=\"utf-8\">\n"
        "<meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">\n<title>");
    esc(o, title);
    buf_puts(o, "</title>\n<link rel=\"stylesheet\" href=\"/_mdserve/style.css\">\n");
    if (user_css) buf_puts(o, "<link rel=\"stylesheet\" href=\"/style.css\">\n");
    buf_puts(o, "</head>\n<body>\n<main class=\"markdown-body\">\n");
}

void page_end(Buf *o) { buf_puts(o, "</main>\n</body>\n</html>\n"); }
