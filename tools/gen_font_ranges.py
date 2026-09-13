#!/usr/bin/env python3
"""Collect the CJK code points that PhyWear actually renders.

Sources:
  * every string in `g_pw_str_zh[]` (phywear_i18n_tables.inc),
  * Chinese string literals passed directly to LVGL in the app sources
    (comments are stripped first, they are not rendered).

Prints the lv_font_conv `-r` range list, e.g. "0x3001-0x3002,0x4E00,...".
"""

import glob
import os
import re
import sys


def strip_comments(src):
    src = re.sub(r"/\*.*?\*/", " ", src, flags=re.S)
    return re.sub(r"//[^\n]*", " ", src)


def decode_c_literal(lit):
    out = bytearray()
    i = 0
    while i < len(lit):
        c = lit[i]
        if c == "\\" and i + 1 < len(lit):
            n = lit[i + 1]
            if n == "x" and re.match(r"[0-9a-fA-F]{2}", lit[i + 2:i + 4]):
                out.append(int(lit[i + 2:i + 4], 16))
                i += 4
                continue
            simple = {"n": 0x0A, "t": 0x09, '"': 0x22, "\\": 0x5C, "r": 0x0D}
            if n in simple:
                out.append(simple[n])
                i += 2
                continue
            i += 2
            continue

        o = ord(c)
        if o < 0x100:
            out.append(o)
        else:
            out.extend(c.encode("utf-8"))
        i += 1

    return out.decode("utf-8", "ignore")


def harvest(text, chars):
    for m in re.finditer(r'"((?:[^"\\]|\\.)*)"', text):
        for ch in decode_c_literal(m.group(1)):
            if ord(ch) >= 0x2E80:
                chars.add(ord(ch))


def main():
    app = sys.argv[1] if len(sys.argv) > 1 else os.path.dirname(
        os.path.abspath(__file__))
    chars = set()

    tables = os.path.join(app, "phywear_i18n_tables.inc")
    src = open(tables, encoding="utf-8", errors="ignore").read()
    harvest(src.split("static const char * const g_pw_str_zh[]", 1)[1], chars)

    for path in glob.glob(os.path.join(app, "*.c")) + \
            glob.glob(os.path.join(app, "*.h")):
        harvest(strip_comments(open(path, encoding="utf-8",
                                    errors="ignore").read()), chars)

    points = sorted(chars)
    ranges = []
    start = prev = None
    for p in points:
        if start is None:
            start = prev = p
            continue
        if p == prev + 1:
            prev = p
            continue
        ranges.append((start, prev))
        start = prev = p
    if start is not None:
        ranges.append((start, prev))

    print(",".join("0x%X" % a if a == b else "0x%X-0x%X" % (a, b)
                   for a, b in ranges))
    print("%d CJK code points" % len(points), file=sys.stderr)


if __name__ == "__main__":
    main()
