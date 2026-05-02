#!/usr/bin/env python3
# bdf2c.py - Converte fonte BDF para font.c do FreeARS
# Uso: python3 bdf2c.py spleen-8x16.bdf > font.c

import sys

def parse_bdf(filename):
    glyphs = {}
    
    with open(filename, 'r') as f:
        lines = f.readlines()
    
    i = 0
    current_char = None
    current_bitmap = []
    in_bitmap = False
    bbx_h = 16
    bbx_w = 8
    bbx_x = 0
    bbx_y = 0
    font_ascent = 14
    font_descent = 2
    
    while i < len(lines):
        line = lines[i].strip()
        
        if line.startswith('FONT_ASCENT'):
            font_ascent = int(line.split()[1])
        elif line.startswith('FONT_DESCENT'):
            font_descent = int(line.split()[1])
        elif line.startswith('ENCODING'):
            current_char = int(line.split()[1])
            current_bitmap = []
            in_bitmap = False
        elif line.startswith('BBX'):
            parts = line.split()
            bbx_w = int(parts[1])
            bbx_h = int(parts[2])
            bbx_x = int(parts[3])
            bbx_y = int(parts[4])
        elif line == 'BITMAP':
            in_bitmap = True
        elif line == 'ENDCHAR':
            if current_char is not None and 0 <= current_char <= 255:
                # Monta glifo 8x16 a partir do bitmap BDF
                glyph = [0] * 16
                
                # Offset vertical: onde começa o glifo na célula 16px
                # baseline = font_ascent (a partir do topo)
                # bbx_y = quantos pixels acima da baseline o glifo começa
                baseline = font_ascent  # linha da baseline no array de 16
                top = baseline - bbx_y - bbx_h  # linha do topo do glifo
                
                for j, row_hex in enumerate(current_bitmap):
                    if not row_hex:
                        continue
                    # Converte hex para byte
                    try:
                        val = int(row_hex, 16)
                    except ValueError:
                        continue
                    
                    # BDF armazena em words de 8 bits, mas pode ser mais largo
                    # Para 8x16, cada linha é 1 byte (2 hex chars)
                    # Pega só o byte mais significativo
                    if len(row_hex) >= 2:
                        byte_val = int(row_hex[:2], 16)
                    else:
                        byte_val = val
                    
                    dest_row = top + j
                    if 0 <= dest_row < 16:
                        glyph[dest_row] = byte_val
                
                glyphs[current_char] = glyph
            
            current_char = None
            current_bitmap = []
            in_bitmap = False
            bbx_h = 16
            bbx_w = 8
            bbx_x = 0
            bbx_y = 0
        elif in_bitmap and line and current_char is not None:
            current_bitmap.append(line)
        
        i += 1
    
    return glyphs

def generate_font_c(glyphs):
    print('// font.c - Spleen 8x16 bitmap font')
    print('// Gerado por bdf2c.py a partir de spleen-8x16.bdf')
    print('// https://github.com/fcambus/spleen')
    print('#include "font.h"')
    print('')
    print('const unsigned char font[256][16] = {')
    
    for code in range(256):
        glyph = glyphs.get(code, [0] * 16)
        
        # Garante 16 linhas
        while len(glyph) < 16:
            glyph.append(0)
        glyph = glyph[:16]
        
        # Comentário com o caractere
        if 32 <= code <= 126:
            char_repr = chr(code)
            if char_repr == '\\':
                char_repr = '\\\\'
            elif char_repr == "'":
                char_repr = "\\'"
            comment = f"/* '{char_repr}' 0x{code:02X} */"
        elif code == 0:
            comment = f"/* NUL 0x{code:02X} */"
        else:
            comment = f"/* 0x{code:02X} */"
        
        row_strs = ', '.join(f'0x{b:02X}' for b in glyph)
        comma = ',' if code < 255 else ''
        print(f'    {{{row_strs}}}{comma} {comment}')
    
    print('};')

def generate_font_h():
    print('// font.h - Spleen 8x16 bitmap font')
    print('#ifndef FONT_H')
    print('#define FONT_H')
    print('')
    print('// font[char_code][row] = 8 bits, bit7 = leftmost pixel')
    print('extern const unsigned char font[256][16];')
    print('')
    print('#endif')

if __name__ == '__main__':
    if len(sys.argv) < 2:
        print(f'Uso: python3 {sys.argv[0]} <arquivo.bdf> [--header]', file=sys.stderr)
        print(f'Exemplos:', file=sys.stderr)
        print(f'  python3 {sys.argv[0]} spleen-8x16.bdf > font.c', file=sys.stderr)
        print(f'  python3 {sys.argv[0]} spleen-8x16.bdf --header > font.h', file=sys.stderr)
        sys.exit(1)
    
    if '--header' in sys.argv:
        generate_font_h()
        sys.exit(0)
    
    filename = sys.argv[1]
    
    try:
        glyphs = parse_bdf(filename)
        print(f'// Parsed {len(glyphs)} glyphs', file=sys.stderr)
        generate_font_c(glyphs)
    except FileNotFoundError:
        print(f'Erro: arquivo {filename} nao encontrado', file=sys.stderr)
        sys.exit(1)
    except Exception as e:
        print(f'Erro: {e}', file=sys.stderr)
        import traceback
        traceback.print_exc(file=sys.stderr)
        sys.exit(1)
