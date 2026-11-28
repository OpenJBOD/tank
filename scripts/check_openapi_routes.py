#!/usr/bin/env python3
"""Fail if the routes registered in src/http/router.c differ from openapi.yaml.

Compares (METHOD, path) pairs. Registrations come from HTTP_RESOURCE_DEFINE in
router.c; methods come from the resource detail each one points at. Only /api/* paths
are compared; the HTML pages are out of scope. Comments are stripped first, the
HTTP and HTTPS services must register the same routes, and the count of
HTTP_RESOURCE_DEFINE uses in src/ must equal the router.c registrations matched.
The spec is read
with a small line scanner, so keep `paths:` entries at two-space indent and
methods at four.
"""
import glob
import re
import sys

METHODS = ("get", "post", "put", "delete", "patch", "head", "options")


def strip_comments(text):
    """Drop /* */ and // comments, leaving string literals alone."""
    return re.sub(
        r'"(?:\\.|[^"\\\n])*"|/\*.*?\*/|//[^\n]*',
        lambda m: m.group(0) if m.group(0)[0] == '"' else " ",
        text, flags=re.S)


def source_routes():
    texts = {}
    for path in glob.glob("src/**/*.c", recursive=True):
        texts[path] = strip_comments(open(path).read())
    router = texts["src/http/router.c"]
    details = {}
    for text in texts.values():
        for m in re.finditer(
            r"struct http_resource_detail_dynamic\s+(\w+)\s*=\s*\{(.*?)\n\};",
            text, re.S):
            mask = re.search(r"bitmask_of_supported_http_methods\s*=\s*([^,]+),", m.group(2))
            if mask:
                details[m.group(1)] = re.findall(r"HTTP_(\w+)", mask.group(1))
    for m in re.finditer(r"DEFINE_(?:AUTH|PUBLIC)_STATIC_RESOURCE\((\w+),", router):
        kind = "auth" if "AUTH" in m.group(0) else "public"
        details["%s_%s_resource_detail" % (m.group(1), kind)] = ["GET"]
    per_service = {}
    matched = 0
    for m in re.finditer(
            r'HTTP_RESOURCE_DEFINE\(\s*\w+,\s*(\w+),\s*"([^"]+)",\s*&(\w+)\)', router):
        matched += 1
        for method in details[m.group(3)]:
            per_service.setdefault(m.group(1), set()).add((method, m.group(2)))
    total = sum(len(re.findall(r"\bHTTP_RESOURCE_DEFINE\(", re.sub(
        r"^\s*#\s*define\s+HTTP_RESOURCE_DEFINE\b", "", text, flags=re.M)))
        for text in texts.values())
    if total != matched:
        sys.exit("%d HTTP_RESOURCE_DEFINE uses in src/, but only %d are literal "
                 "registrations in router.c; the extractor misses some routes"
                 % (total, matched))
    services = sorted(per_service)
    if services != ["test_http_service", "test_https_service"]:
        sys.exit("expected test_http_service and test_https_service, got %s" % services)
    http, https = per_service["test_http_service"], per_service["test_https_service"]
    if http != https:
        for label, items in (("HTTP only", http - https), ("HTTPS only", https - http)):
            for method, path in sorted(items, key=lambda x: (x[1], x[0])):
                print("%s: %s %s" % (label, method, path))
        sys.exit(1)
    return {r for r in http if r[1].startswith("/api/")}


def spec_routes(path="openapi.yaml"):
    found, cur, in_paths = set(), None, False
    for line in open(path):
        if re.match(r"\S", line):
            in_paths = line.startswith("paths:")
            continue
        if not in_paths:
            continue
        m = re.match(r"  (/\S*):\s*$", line)
        if m:
            cur = m.group(1)
            continue
        m = re.match(r"    (\w+):\s*$", line)
        if m and cur and m.group(1) in METHODS:
            if cur.startswith("/api/"):
                found.add((m.group(1).upper(), cur))
    return found


src, spec = source_routes(), spec_routes()
if not src:
    sys.exit("no routes found in src/http/router.c; the extractor is out of date")
for label, items in (("in source, missing from openapi.yaml", src - spec),
                     ("in openapi.yaml, not registered in source", spec - src)):
    for method, path in sorted(items, key=lambda x: (x[1], x[0])):
        print("%s: %s %s" % (label, method, path))
if src != spec:
    sys.exit(1)
print("%d routes match" % len(src))
