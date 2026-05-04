#!/usr/bin/env python3
# bm2ph.py - Converts sthing

import sys

if len(sys.argv) < 4:
    print("Usage: python bmp2h.py input.bmp output.h varname")
    sys.exit(1)

with open(sys.argv[1], "rb") as f:
    data = f.read()

name = sys.argv[3]
size_name = name + "_size"

with open(sys.argv[2], "w") as out:
    out.write("#pragma once\n")
    out.write("#include <stdint.h>\n\n")
    out.write(f"static const uint8_t {name}[] = {{\n")
    for i, b in enumerate(data):
        out.write(f"0x{b:02X}, ")
        if (i + 1) % 16 == 0:
            out.write("\n")
    out.write("\n};\n")
    out.write(f"static const uint32_t {size_name} = sizeof({name});\n")

print(f"Generated: {sys.argv[2]}")