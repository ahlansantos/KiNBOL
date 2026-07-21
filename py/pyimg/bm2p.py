#!/usr/bin/env python3
# bm2p.py - Converts sthing

import sys, os
from PIL import Image

if len(sys.argv) < 2:
    print("Usage: python png2bmp.py image.png")
    print("Output: image.bmp in same folder")
    sys.exit(1)

png_path = sys.argv[1]
bmp_path = os.path.splitext(png_path)[0] + ".bmp"

img = Image.open(png_path).convert("RGB")
img.save(bmp_path, "BMP")
print(f"Saved: {bmp_path}")