<p align="center">
  <img src="https://github.com/lordpaijo/medicine/blob/master/logo.png" alt="Medicine Logo" width="200"/>
</p>

# medicine
Medicine (from "MD-C-ine", haha) is a tiny HTTP server in C that renders Markdown as HTML.

Build:
```shell
$ make
```
Run:
```shell
$ ./medicine [-p port] [-b bind-address] [docroot]
$ ./medicine -p 8080 ./site
```
### What it does:
 - `*.md` / `*.markdown` files are rendered to HTML with a built-in stylesheet (add `?raw` to the URL to see the Markdown source instead)
 - everything else (`.html`, `.css`, `.js`, images, ...) is served as-is
 - directories serve `index.md`, `README.md` or `index.html`, or a file listing
 - `/foo` falls back to `/foo.md` or `/foo.html`
 - if docroot contains a `style.css` it is loaded after the built-in CSS, so you can restyle rendered pages without touching this file
### Markdown supported: 
 - headings, paragraphs, **bold**, *italic*, ~~strike~~,
 - `code`, fenced code blocks, links, images (and image links, e.g. badges),
### What can you do with it?
 - write simple websites (blogs, news, comics, etc)
 - write online digital books.
---
**One forked process per connection, GET/HEAD only, no TLS, no keep-alive.**
**Hobby project: don't put it on the open internet.**
