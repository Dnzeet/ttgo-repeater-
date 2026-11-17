#!/usr/bin/env python3
"""Buat font Adafruit-GFX (1-bit) dari Inter untuk src/fonts.h.
Pakai: python3 tools/genfont.py <path-ke-Inter.otf ...>  (hasil sudah ikut di repo)"""
import sys
from PIL import Image, ImageDraw, ImageFont

FIRST, LAST = 32, 126

def build(name, path, px, thresh=110):
    f = ImageFont.truetype(path, px)
    asc, desc = f.getmetrics()
    bitmap = bytearray(); glyphs = []
    for code in range(FIRST, LAST + 1):
        ch = chr(code)
        adv = int(round(f.getlength(ch)))
        im = Image.new("L", (px * 3, px * 3), 0)
        ImageDraw.Draw(im).text((px, px * 2), ch, font=f, fill=255, anchor="ls")
        im = im.point(lambda v: 255 if v >= thresh else 0)
        bb = im.getbbox()
        off = len(bitmap)
        if bb is None:
            glyphs.append((off, 0, 0, adv, 0, 0)); continue
        x0, y0, x1, y1 = bb
        w, h = x1 - x0, y1 - y0
        bits = 0; n = 0
        for y in range(y0, y1):
            for x in range(x0, x1):
                bits = (bits << 1) | (1 if im.getpixel((x, y)) else 0); n += 1
                if n == 8:
                    bitmap.append(bits); bits = 0; n = 0
        if n: bitmap.append(bits << (8 - n))
        glyphs.append((off, w, h, adv, x0 - px, y0 - px * 2))
    yadv = asc + desc
    out = [f"// {name}: {path.split('/')[-1]} @ {px}px"]
    out.append(f"const uint8_t {name}Bitmaps[] PROGMEM = {{")
    out.append(",".join(f"0x{b:02X}" for b in bitmap)); out.append("};")
    out.append(f"const GFXglyph {name}Glyphs[] PROGMEM = {{")
    out.append(",\n".join("{%d,%d,%d,%d,%d,%d}" % g for g in glyphs)); out.append("};")
    out.append(f"const GFXfont {name} PROGMEM = {{(uint8_t*){name}Bitmaps,(GFXglyph*){name}Glyphs,{FIRST},{LAST},{yadv}}};\n")
    return "\n".join(out)

if __name__ == "__main__":
    base = "/usr/share/fonts/opentype/inter/"
    parts = ["// Dibuat oleh tools/genfont.py dari Inter (SIL OFL 1.1). Jangan diedit manual.",
             "#pragma once", "#include <Arduino.h>", "#include <TFT_eSPI.h>", ""]
    parts.append(build("InterS",  base + "Inter-Medium.otf",   11))   # label kecil
    parts.append(build("InterM",  base + "Inter-Medium.otf",   14))   # teks nilai
    parts.append(build("InterMB", base + "Inter-SemiBold.otf", 14))   # nilai tegas
    parts.append(build("InterL",  base + "Inter-SemiBold.otf", 22))   # angka besar
    open("src/fonts.h", "w").write("\n".join(parts))
    print("ok")
