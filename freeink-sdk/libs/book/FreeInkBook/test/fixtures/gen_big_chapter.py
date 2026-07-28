#!/usr/bin/env python3
"""Generates a large chapter for the minimal fixture so the streaming reader
is exercised past the 32 KB inflate window (multiple wraps)."""
import sys

OUT = sys.argv[1]
PARAGRAPHS = 600

with open(OUT, "w", encoding="utf-8") as f:
    f.write('<?xml version="1.0" encoding="UTF-8"?>\n')
    f.write('<html xmlns="http://www.w3.org/1999/xhtml">\n')
    f.write("<head><title>Chapter Two</title></head>\n<body>\n")
    f.write("<h1>Chapter Two</h1>\n")
    f.write("<p>BIG-CHAPTER-START-MARKER</p>\n")
    for i in range(PARAGRAPHS):
        f.write(
            f"<p>Paragraph {i:04d}: the quick brown fox jumps over the lazy "
            "dog while the streaming inflate window wraps around its ring "
            "buffer again and again, verifying that page-sized reads survive "
            "chapter-sized documents without chapter-sized memory.</p>\n"
        )
    f.write("<p>BIG-CHAPTER-END-MARKER</p>\n")
    f.write("</body>\n</html>\n")
