section .rodata

global font_ttf_data
global font_ttf_data_end

font_ttf_data:
    incbin "src/graphics/assets/font.ttf"
font_ttf_data_end: