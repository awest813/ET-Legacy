#!/usr/bin/env python3
"""Pack the repository's loose etmain assets into etloose.pk3 for the web build."""
import os
import sys
import zipfile

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
SRC = os.path.join(ROOT, "etmain")
OUT = sys.argv[1] if len(sys.argv) > 1 else os.path.join(ROOT, "build_wasm", "etloose.pk3")

count = 0
with zipfile.ZipFile(OUT, "w", zipfile.ZIP_DEFLATED, compresslevel=6) as z:
    for dirpath, _dirnames, filenames in os.walk(SRC):
        for fn in filenames:
            full = os.path.join(dirpath, fn)
            rel = os.path.relpath(full, SRC).replace("\\", "/")
            # engine reads pk3s with forward slashes, no leading ./ 
            z.write(full, rel)
            count += 1

print(f"etloose.pk3: packed {count} files -> {OUT}")
