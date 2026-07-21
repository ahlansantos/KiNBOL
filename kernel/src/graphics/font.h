<<<<<<< HEAD
/*
 * Header for the font module. Just declares the font[256][16] array the
 * terminal uses to draw text.
 */
=======
>>>>>>> origin/x86_64-uefi
// font.h - Spleen 8x16 bitmap font
#ifndef FONT_H
#define FONT_H

// font[char_code][row] = 8 bits, bit7 = leftmost pixel
extern const unsigned char font[256][16];

#endif
