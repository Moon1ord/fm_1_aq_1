#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
"""The screen's art, made here and kept in git (app/art_data.h), as tools/gen_breaks.c makes the break's sounds.

The owner's pictures are not in this repository (15 MB of PNG, and they are the owner's): they live next to it, in
../art/source of the liquid folder, and tools_art/ beside them has the scripts that gave the approved previews. So a clean checkout builds from
the header alone and never runs this; only changing the pictures needs them.

    .venv/bin/python3 -I tools/gen_art.py [--src ../art/source] [--out app/art_data.h]
    .venv/bin/python3 -I tools/gen_art.py --check     the header is what the sources give, and the frames equal the approved previews (exit 1)

A picture is its tones, 8 of them, kept as runs row by row: a byte is the run's length - 1 in the high five bits, the tone in the low three. Each row
starts afresh and has its offset in a table, so a strip can start anywhere and a frame is never held in RAM.
The colours are applied when it is drawn (ART_LUT), so the dim and frozen looks of a picture cost no flash: a table each. Row parity is the scanlines.
The face is one picture (the closed cell) and patches of the other cells around its eyes and its mouth: their noise differs in 10-14 % of the pixels. """
import argparse, os, sys
from PIL import Image, ImageFilter, ImageOps

HERE = os.path.dirname(os.path.abspath(__file__))
PAL = [(8,10,44),(22,28,92),(44,58,146),(72,94,194),(108,132,222),(150,172,238),(198,214,250),(250,252,255)]
SCAN = .88
# the chapters, as tools_art/chapters.py: page, column and row of the sheet, how far down the panel the square starts (the crop fixes)
CHAPTERS = [('KEYS',0,0,.33),('MELODY',1,0,.14),('BASS',2,0,.20),('DRUMS',0,1,.06),('SPACE',1,1,.08),('SETUP',2,1,.12)]
# the patches on the closed cell: x, y, w, h, and the feather of their edge (the cells' noise melts into the base over it)
EYES = (56, 47, 96, 48)
MOUTH = (116, 98, 46, 28)
FEATHER = 5
# the looks: tones down by so many, a tint, and a veil of light. HOLD is the previews' (tone -1, tint .82 1.05 1.16, a veil of 70), about half as strong: the owner asked for the ice weaker
LOOKS = [('NORMAL',0,None,None),('DIM1',1,None,None),('DIM2',2,None,None),('DIM3',3,None,None),('HOLD',0,(.91,1.03,1.08),((190,235,255),32))]

def tone(v): return min(7, v * 8 // 256)

def cell(im, box=None):                         # all_screens.py's cell()
    if box: im = im.crop(box)
    s = min(im.size); im = im.crop((0,0,s,s))
    g = ImageOps.autocontrast(im.filter(ImageFilter.GaussianBlur(s/480)).resize((240,240), Image.LANCZOS), cutoff=1)
    return g.point(lambda v: int(255 * (v / 255) ** 1.25))

def chapter(im, cx, cy, off):                   # chapters.py's crop and tone curve
    W, H = im.size; pw, ph = W // 3, H // 2
    x0 = cx * pw; y0 = cy * ph + int(off * ph)
    c = im.crop((x0 + 3, y0, x0 + pw - 3, y0 + pw - 6))
    g = ImageOps.autocontrast(c.filter(ImageFilter.GaussianBlur(pw / 480)).resize((240,240), Image.LANCZOS), cutoff=1)
    return g.point(lambda v: int(255 * (v / 255) ** 1.15))

def tones_of(g):
    a = g.load()
    return [[tone(a[x,y]) for x in range(240)] for y in range(240)]

def patch(base, other, box):                    # the other cell's greys, melting into the base's at the edge
    x0, y0, w, h = box; b = base.load(); o = other.load(); rows = []
    for y in range(y0, y0+h):
        r = []
        for x in range(x0, x0+w):
            d = min(x-x0, x0+w-1-x, y-y0, y0+h-1-y)
            t = min(1.0, d / FEATHER)
            r.append(tone(int(b[x,y] * (1-t) + o[x,y] * t + .5)))
        rows.append(r)
    return rows

def encode(rows):
    data = bytearray(); off = []
    for r in rows:
        off.append(len(data)); i = 0
        while i < len(r):
            j = i
            while j < len(r) and r[j] == r[i] and j - i < 32: j += 1
            data.append((j - i - 1) << 3 | r[i]); i = j
    off.append(len(data))
    return bytes(data), off

def crc32(rows):                                # of the tones, for tests/host/art_test.c
    import zlib
    return zlib.crc32(bytes(v for r in rows for v in r))

def rgb565_swapped(c):
    v = (c[0] & 0xF8) << 8 | (c[1] & 0xFC) << 3 | c[2] >> 3
    return (v >> 8 | v << 8) & 0xFFFF

def look(t, name_dim, tint, veil, odd):
    c = PAL[max(0, t - name_dim)]
    if tint: c = tuple(min(255, int(c[i] * tint[i])) for i in range(3))
    if veil: c = tuple(min(255, (c[i] * (255 - veil[1]) + veil[0][i] * veil[1]) // 255) for i in range(3))
    if odd: c = tuple(int(k * SCAN) for k in c)
    return c

def build(src):
    L = lambda n: Image.open(os.path.join(src, n)).convert('L')
    eyes = L('eyes.png'); W, H = eyes.size
    half, opened, closed, smile = (cell(eyes, (i%2*W//2, i//2*H//2, (i%2+1)*W//2, (i//2+1)*H//2)) for i in range(4))
    pics = [('FACE', (0,0,240,240), tones_of(closed))]
    pics.append(('TITLE', (0,0,240,240), tones_of(cell(L('title.png')))))
    pics.append(('BREAK', (0,0,240,240), tones_of(cell(L('break.png')))))
    sheet = L('chapters.png')
    for n, cx, cy, off in CHAPTERS:
        pics.append((n, (0,0,240,240), tones_of(chapter(sheet, cx, cy, off))))
    pics.append(('EYE_HALF', EYES, patch(closed, half, EYES)))
    pics.append(('EYE_OPEN', EYES, patch(closed, opened, EYES)))
    pics.append(('MOUTH_SMILE', MOUTH, patch(closed, smile, MOUTH)))
    return pics

def header(pics):
    o = ['/* SPDX-License-Identifier: MPL-2.0 */',
         '/* The screen\'s art: generated by tools/gen_art.py from the owner\'s pictures (see there), do not edit. */',
         '#pragma once', '#include <stdint.h>', '']
    total = 0
    for n, box, rows in pics:
        d, off = encode(rows)
        total += len(d) + 2 * len(off)
        o.append('static const uint8_t ART_%s_D[%d] = {' % (n, len(d)))
        for i in range(0, len(d), 32):
            o.append(','.join(str(b) for b in d[i:i+32]) + ',')
        o.append('};')
        o.append('static const uint16_t ART_%s_R[%d] = {' % (n, len(off)))
        for i in range(0, len(off), 24):
            o.append(','.join(str(b) for b in off[i:i+24]) + ',')
        o.append('};')
    o.append('enum { ' + ', '.join('ART_%s' % n for n, _, _ in pics) + ', ART_N };')
    o.append('static const art_t ART[ART_N] = {')
    for n, box, rows in pics:
        o.append('    {ART_%s_D, ART_%s_R, %d, %d, %d, %d, 0x%08Xu},' % (n, n, box[0], box[1], box[2], box[3], crc32(rows)))
    o.append('};')
    o.append('enum { ' + ', '.join('LOOK_%s' % l[0] for l in LOOKS) + ', LOOK_N };')
    o.append('/* byte-swapped RGB565 as the canvas keeps it: look, row parity (odd rows: the scanlines), tone */')
    o.append('static const uint16_t ART_LUT[LOOK_N][2][8] = {')
    for nm, dim, tint, veil in LOOKS:
        o.append('    {' + ','.join('{' + ','.join('0x%04X' % rgb565_swapped(look(t, dim, tint, veil, odd)) for t in range(8)) + '}' for odd in (0,1)) + '},')
    o.append('};')
    o.append('#define ART_BYTES %du' % total)
    return '\n'.join(o) + '\n', total

def frame_rgb(rows):
    q = Image.new('RGB', (240,240)); P = q.load()
    for y in range(240):
        for x in range(240):
            c = look(rows[y][x], 0, None, None, y & 1)
            P[x,y] = (c[0] & 0xF8, c[1] & 0xFC, c[2] & 0xF8)
    return q

def check_preview(pics, pre):                   # what a chapter is, against what was approved
    bad = 0
    names = {'KEYS':'keys','MELODY':'melody','BASS':'bass','DRUMS':'drums','SPACE':'space','SETUP':'setup'}
    for n, box, rows in pics:
        if n in names:
            a = frame_rgb(rows).tobytes(); b = Image.open(os.path.join(pre, 'chapter_%s_plain.png' % names[n])).convert('RGB').tobytes()
            d = sum(1 for i in range(0, len(a), 3) if a[i:i+3] != b[i:i+3])
            print(' ', n, 'pixels differing from the approved chapter:', d)
            bad += d != 0
    # the others under their panels, where the previews show them bare: the screens of tools_art/all_screens.py, by rows and columns
    for n, f, x0, y0, x1, y1 in (('TITLE', 'all_00_1x.png', 0, 0, 240, 150), ('BREAK', 'all_10_1x.png', 0, 0, 240, 180), ('FACE', 'all_01_1x.png', 70, 66, 200, 140)):
        rows = next(r for m, _, r in pics if m == n)
        a = frame_rgb(rows).crop((x0, y0, x1, y1)).tobytes(); b = Image.open(os.path.join(pre, f)).convert('RGB').crop((x0, y0, x1, y1)).tobytes()
        d = sum(1 for i in range(0, len(a), 3) if a[i:i+3] != b[i:i+3])
        print(' ', n, 'pixels differing from', f, 'in', (x0, y0, x1, y1), ':', d)
        bad += d != 0
    return bad

def main():
    root = os.path.dirname(HERE)
    p = argparse.ArgumentParser()
    p.add_argument('--src', default=os.path.join(root, '..', 'art', 'source'))
    p.add_argument('--out', default=os.path.join(root, 'app', 'art_data.h'))
    p.add_argument('--check', action='store_true')
    a = p.parse_args()
    pics = build(a.src)
    text, total = header(pics)
    if a.check:
        bad = check_preview(pics, os.path.join(a.src, '..', 'preview'))
        same = open(a.out).read() == text
        print('  header', 'is what the sources give' if same else 'is NOT what the sources give')
        sys.exit(1 if bad or not same else 0)
    with open(a.out, 'w') as f:
        f.write(text)
    for n, box, rows in pics:
        print('%-12s %3dx%-3d %6d B of runs' % (n, box[2], box[3], len(encode(rows)[0])))
    print('total with row tables', total, 'B')

if __name__ == '__main__':
    main()
