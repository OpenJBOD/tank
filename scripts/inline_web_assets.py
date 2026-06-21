#!/usr/bin/env python3
"""Inline a page's CSS/JS into its HTML so it loads in a single HTTP(S) request.

Over HTTPS on the RP2040 the TLS handshake (~1.6-2.8s) dominates and handshakes
serialise on the single crypto core, so each extra sub-resource (style.css, the
page's own JS, common.js) adds a whole handshake to the page load. Inlining them
collapses a page to one request -> one handshake.

Usage: inline_web_assets.py <page.html> <web_resources_dir> <output.html>

Replaces  <link rel="stylesheet" href="...css">  with  <style>...</style>
and       <script src="...js"></script>           with  <script>...</script>
for any referenced file found in <web_resources_dir>. References that don't resolve
to a local file are left untouched.
"""
import os
import re
import sys


def main():
    if len(sys.argv) != 4:
        sys.exit("usage: inline_web_assets.py <page.html> <web_dir> <output.html>")

    html_path, web_dir, out_path = sys.argv[1], sys.argv[2], sys.argv[3]

    with open(html_path, encoding="utf-8") as f:
        html = f.read()

    def read_asset(ref):
        """Resolve an href/src to a file in web_dir; return its contents or None."""
        path = os.path.join(web_dir, os.path.basename(ref))
        if not os.path.isfile(path):
            return None
        with open(path, encoding="utf-8") as f:
            return f.read()

    def inline_css(match):
        content = read_asset(match.group(1))
        if content is None:
            return match.group(0)
        return "<style>\n" + content + "</style>"

    def inline_js(match):
        content = read_asset(match.group(1))
        if content is None:
            return match.group(0)
        # Defensive: keep a literal </script> in the source from ending the tag.
        content = content.replace("</script>", "<\\/script>")
        return "<script>\n" + content + "</script>"

    html = re.sub(r'<link\s+rel="stylesheet"\s+href="([^"]+\.css)"\s*/?>',
                  inline_css, html)
    html = re.sub(r'<script\s+src="([^"]+\.js)"\s*></script>',
                  inline_js, html)

    os.makedirs(os.path.dirname(out_path), exist_ok=True)
    with open(out_path, "w", encoding="utf-8") as f:
        f.write(html)


if __name__ == "__main__":
    main()
