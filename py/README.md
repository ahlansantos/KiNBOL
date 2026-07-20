# py/

Offline asset-conversion scripts. These aren't part of the kernel build —
run them by hand whenever you need to (re)generate an embedded asset, then
commit the generated `.c`/`.h` into the kernel tree.

## pyfont/

- **bdf2c.py** — Converts a BDF bitmap font (e.g. Spleen) into `font.c` /
  `font.h`, an 8x16 `const unsigned char font[256][16]` glyph table
  consumed by `kernel/src/graphics/font.c`.

  ```
  python3 bdf2c.py spleen-8x16.bdf > font.c
  python3 bdf2c.py spleen-8x16.bdf --header > font.h
  ```

## pyimg/

Two-step pipeline to embed an image directly into the kernel binary
(no filesystem/loader needed to display it):

1. **bm2p.py** — PNG → uncompressed BMP (RGB, no alpha).
   ```
   python3 bm2p.py image.png     # writes image.bmp next to it
   ```
2. **bm2ph.py** — BMP → C header (`static const uint8_t name[]` + a
   `name_size` constant).
   ```
   python3 bm2ph.py image.bmp image.h image
   ```