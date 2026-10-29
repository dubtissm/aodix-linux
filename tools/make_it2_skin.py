#!/usr/bin/env python3
"""Builds the 'Impulse' skin (colors and feel of Impulse Tracker 2) from the Blue skin.

usage: tools/make_it2_skin.py resources/Skins/Blue resources/Skins/Impulse

The grey interface of Blue is mapped by brightness onto the tan/brown/black palette of IT2, the font
strip is redrawn in a bolder DOS-like style with IT colors, and the background artwork is replaced.
"""
import sys, math, os
from PIL import Image, ImageDraw

KEY = (0, 255, 0)          # transparent key color of the skins

# IT2 palette (VGA 6-bit values * 4)
BLACK   = (0, 0, 0)
DKBROWN = (60, 40, 24)
SHADOW  = (124, 88, 68)
FACE    = (192, 156, 122)
LIGHT   = (216, 192, 160)
HILIGHT = (232, 232, 200)
YELLOW  = (252, 252, 84)
GREEN   = (68, 160, 72)
LTGREEN = (120, 220, 120)
MAGENTA = (176, 0, 84)
DKRED   = (76, 12, 24)

# brightness ramp for unsaturated pixels (luma -> color)
RAMP = [(0, BLACK), (112, (8, 6, 4)), (128, DKBROWN), (150, SHADOW), (172, FACE),
        (196, (204, 172, 138)), (222, LIGHT), (240, HILIGHT), (256, (252, 252, 228))]

def lerp(a, b, t):
    return tuple(int(round(a[i] + (b[i] - a[i]) * t)) for i in range(3))

def ramp(l):
    for (l0, c0), (l1, c1) in zip(RAMP, RAMP[1:]):
        if l < l1:
            return lerp(c0, c1, (l - l0) / (l1 - l0))
    return RAMP[-1][1]

def luma(p):
    return 0.299 * p[0] + 0.587 * p[1] + 0.114 * p[2]

def sat(p):
    return max(p) - min(p)

def recolor(p):
    if p == KEY:
        return p
    if sat(p) < 34:
        return ramp(luma(p))
    # colored parts (play/stop/rec buttons, vu meter, markers) keep their hue, slightly warmer
    r, g, b = p
    return (min(255, int(r * 1.04)), g, int(b * 0.9))

def recolor_image(im, box=None):
    px = im.load()
    x0, y0, x1, y1 = box or (0, 0, im.width, im.height)
    cache = {}
    for y in range(y0, y1):
        for x in range(x0, x1):
            p = px[x, y]
            if p not in cache:
                cache[p] = recolor(p)
            px[x, y] = cache[p]

# ------------------------------------------------------------------------------------------------
# font strip: 6 rows of 8 pixels at y=588, 6 pixels per character (character code * 6)
FONT_Y = 588
# (background, text) per row; backgrounds follow the recolored fields the text is drawn on
FONT_ROWS = [
    (None, BLACK),        # 0: labels on light fields
    (None, GREEN),        # 1: parameters, most of the text (dark fields)
    (None, YELLOW),       # 2: selected instance / vst parameter
    (None, BLACK),        # 3: master/edit time indicators, cpu, number of events
    (None, LTGREEN),      # 4: track indexes
    (None, YELLOW),       # 5: selected track label / marker labels
]

def redo_font(src, dst):
    s = src.load()
    d = dst.load()
    for row, (bg, fg) in enumerate(FONT_ROWS):
        y0 = FONT_Y + row * 8
        # background = most common color of the row in the source
        counts = {}
        for x in range(dst.width):
            for y in range(y0, y0 + 8):
                counts[s[x, y]] = counts.get(s[x, y], 0) + 1
        src_bg = max((c for c in counts if c != KEY), key=lambda c: counts[c])
        new_bg = bg or recolor(src_bg)
        for cx in range(0, dst.width - 5, 6):
            # glyph mask
            m = [[s[cx + x, y0 + y] != src_bg and s[cx + x, y0 + y] != KEY for x in range(6)] for y in range(8)]
            if any(s[cx + x, y0 + y] == KEY for x in range(6) for y in range(8)):
                continue
            # bold: thicken vertical strokes to the right (stays inside columns 0..4)
            b = [[m[y][x] or (x > 0 and x < 5 and m[y][x - 1]) for x in range(6)] for y in range(8)]
            for y in range(8):
                for x in range(6):
                    d[cx + x, y0 + y] = fg if b[y][x] else new_bg

def glyph_text(img, x, y, text, row, fg=None, bg=None):
    """draws text with the (already converted) font strip of img"""
    px = img.load()
    ref_bg = px[0, FONT_Y + row * 8]
    for i, ch in enumerate(text):
        sx = ord(ch) * 6
        for yy in range(8):
            for xx in range(6):
                p = px[sx + xx, FONT_Y + row * 8 + yy]
                on = p != ref_bg
                if on:
                    px[x + i * 6 + xx, y + yy] = fg or p
                elif bg is not None:
                    px[x + i * 6 + xx, y + yy] = bg

def bevel(d, x0, y0, x1, y1, sunken, light=HILIGHT, dark=SHADOW):
    a, b = (dark, light) if sunken else (light, dark)
    d.line([(x0, y0), (x1, y0)], fill=a)
    d.line([(x0, y0), (x0, y1)], fill=a)
    d.line([(x0, y1), (x1, y1)], fill=b)
    d.line([(x1, y0), (x1, y1)], fill=b)

def waveform(d, x0, y0, x1, y1, seed):
    """an IT style sample display: a decaying tone in magenta on black"""
    d.rectangle([x0, y0, x1, y1], fill=BLACK)
    mid = (y0 + y1) / 2
    h = (y1 - y0) / 2 - 3
    attack = 0.02 + 0.03 * seed
    decay = 2.6 + seed
    prev = None
    for x in range(x0 + 1, x1):
        t = (x - x0) / (x1 - x0)
        env = math.exp(-decay * t) * min(1.0, t / attack)
        v = math.sin((x - x0) * 2.1 + seed) * env * (0.85 + 0.15 * math.sin(t * 40 + seed))
        y = mid - v * h
        if prev:
            d.line([prev, (x, y)], fill=MAGENTA)
        prev = (x, y)
    d.line([(x0 + 1, mid), (x1 - 1, mid)], fill=(60, 0, 30))

def main(src_dir, dst_dir):
    os.makedirs(dst_dir, exist_ok=True)

    # ---------------- gfx.bmp
    src = Image.open(os.path.join(src_dir, 'gfx.bmp')).convert('RGB')
    gfx = src.copy()
    recolor_image(gfx)
    redo_font(src, gfx)
    d = ImageDraw.Draw(gfx)

    # background color of the sequencer (pixel 0,0): black like the IT pattern editor
    gfx.putpixel((0, 0), (8, 8, 8))

    # instance parameter background (artwork of Blue): black panel with a faint dotted grid
    ax0, ay0, ax1, ay1 = 320, 0, 576, 176
    d.rectangle([ax0, ay0, ax1 - 1, ay1 - 1], fill=BLACK)
    for y in range(ay0 + 7, ay1, 8):
        for x in range(ax0 + 3, ax1, 4):
            gfx.putpixel((x, y), (28, 28, 24))
    glyph_text(gfx, ax0 + 4, ay1 - 20, '"impulse" skin for aodix', 1, fg=(60, 120, 64))
    glyph_text(gfx, ax0 + 4, ay1 - 11, 'based on "blue" by paul merchant', 1, fg=(60, 120, 64))

    gfx.save(os.path.join(dst_dir, 'gfx.bmp'))

    # ---------------- gui.bmp
    gui = Image.open(os.path.join(src_dir, 'gui.bmp')).convert('RGB')
    recolor_image(gui, (0, 0, 1024, gui.height))
    d = ImageDraw.Draw(gui)
    # right part (artwork behind the window on wide screens): tan desk with sample displays
    d.rectangle([1024, 0, gui.width - 1, gui.height - 1], fill=FACE)
    boxes = [(1032, 8, 1500, 110), (1032, 120, 1500, 224), (1508, 8, 2040, 224)]
    for i, (x0, y0, x1, y1) in enumerate(boxes):
        bevel(d, x0 - 2, y0 - 2, x1 + 2, y1 + 2, False)
        bevel(d, x0 - 1, y0 - 1, x1 + 1, y1 + 1, True)
        waveform(d, x0, y0, x1, y1, i * 0.6)
    gui.save(os.path.join(dst_dir, 'gui.bmp'))

    # ---------------- knb.bmp (knobs)
    knb = Image.open(os.path.join(src_dir, 'knb.bmp')).convert('RGB')
    recolor_image(knb)
    knb.save(os.path.join(dst_dir, 'knb.bmp'))

if __name__ == '__main__':
    main(sys.argv[1], sys.argv[2])
