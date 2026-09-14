#!/usr/bin/env python3
"""Emit a TSV of legacy highlights resolved against an extracted EPUB.

Feeds tools/migration_dryrun, which runs the firmware's own MigrationPlanner
over the rows. Reads the user's data from paths given on the command line and
writes only addresses and references -- never passage text -- so nothing
copyrighted is produced.
"""
import argparse
import json
import os
import re
import sys


def parse_opf(epub_dir):
    opf = None
    for root, _, files in os.walk(epub_dir):
        for f in files:
            if f.endswith(".opf"):
                opf = os.path.join(root, f)
                break
        if opf:
            break
    if not opf:
        sys.exit("no .opf found under %s" % epub_dir)

    text = open(opf, encoding="utf-8").read()
    manifest = {}
    for m in re.finditer(r"<item\b([^>]*)>", text):
        attrs = m.group(1)
        i = re.search(r'\bid="([^"]+)"', attrs)
        h = re.search(r'\bhref="([^"]+)"', attrs)
        if i and h:
            manifest[i.group(1)] = h.group(1)
    spine = [manifest.get(m.group(1), "") for m in re.finditer(r'<itemref\b[^>]*idref="([^"]+)"', text)]
    return os.path.dirname(opf), spine


def build_book_map(base, spine):
    """Filename tail -> canonical book number, 1-66.

    biblebooknav.xhtml lists the books in canonical order. Its targets are
    chapter-nav pages, except for the five single-chapter books that point
    straight at a spine item -- so each target has to be followed.
    """
    nav = os.path.join(base, "biblebooknav.xhtml")
    if not os.path.exists(nav):
        return {}

    def hrefs(path):
        html = open(path, encoding="utf-8", errors="replace").read()
        return [h.split("#")[0].split("/")[-1] for h in re.findall(r'<a\b[^>]*href="([^"]+)"', html)]

    books = [h for h in hrefs(nav) if h]
    out = {}
    for index, target in enumerate(books, start=1):
        if target.startswith("biblechapternav"):
            page = os.path.join(base, target)
            if not os.path.exists(page):
                continue
            for chapter in hrefs(page):
                if chapter and not chapter.startswith("biblebooknav"):
                    out[chapter] = index
        else:
            out[target] = index
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--epub-dir", required=True)
    ap.add_argument("--highlights", required=True)
    ap.add_argument("--out", required=True)
    args = ap.parse_args()

    base, spine = parse_opf(args.epub_dir)
    book_map = build_book_map(base, spine)

    doc = json.load(open(args.highlights, encoding="utf-8"))
    entries = doc.get("h") or doc.get("highlights") or []
    tags = doc.get("t") or doc.get("tags") or []

    written = 0
    with open(args.out, "w", encoding="utf-8") as out:
        for e in entries:
            si = e.get("si", 0)
            if si >= len(spine):
                continue
            name = os.path.basename(spine[si])
            path = os.path.join(base, name)
            out.write("%d\t%d\t%d\t%s\t%s\t%d\n" % (
                si, e.get("start", 0), e.get("end", 0), e.get("ref", ""), path, book_map.get(name, 0)))
            written += 1

    sys.stderr.write("rows=%d  spine=%d  books mapped=%d  palette=%d\n"
                     % (written, len(spine), len(set(book_map.values())), len(tags)))


if __name__ == "__main__":
    main()
