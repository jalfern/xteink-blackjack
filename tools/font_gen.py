#!/usr/bin/env python3
"""Render a real typeface into src/gfx_font.h (1bpp atlas + suit glyphs).

The first version of this project hand-typed 5x7 bitmaps. On a 3.68in 792x528
panel that is 259 PPI - a 5x7 glyph is about 0.5mm tall. That is a font for a
128x64 LCD, and it looked like one. This renders actual Helvetica Neue at UI
sizes plus a bold face for card ranks, and prints a preview so quality is
judged here rather than on hardware.

Bitmap format per size:
  fixed cell height (ascent+descent), stride = (cellW+7)/8 bytes per row,
  bit 0x80 = leftmost pixel, 1 = ink.
  META[95][4] = { advance, inkWidth, topOffset, reserved }
"""
import os
from PIL import Image, ImageDraw, ImageFont

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
OUT = os.path.join(ROOT, "src", "gfx_font.h")

REG = "/System/Library/Fonts/HelveticaNeue.ttc"
FACE_REG, FACE_BOLD = 0, None

def pick(path, want_bold):
    """Find the requested face inside a .ttc by asking PIL for its name."""
    for i in range(12):
        try:
            f = ImageFont.truetype(path, 32, index=i)
        except Exception:
            break
        fam, style = f.getname()
        if want_bold and style.lower().startswith("bold"):
            print(f"  face: {fam} {style} (index {i})")
            return i
        if not want_bold and style.lower() == "regular":
            print(f"  face: {fam} {style} (index {i})")
            return i
    print(f"  face: falling back to index 0 of {path}")
    return 0

IDX_REG = pick(REG, False)
BOLD_CANDIDATES = [
    "/System/Library/Fonts/Supplemental/Arial Bold.ttf",
    "/System/Library/Fonts/Supplemental/Helvetica Bold.ttf",
]
BOLD_PATH = next((p for p in BOLD_CANDIDATES if os.path.exists(p)), REG)
IDX_BOLD = pick(BOLD_PATH, True) if BOLD_PATH.endswith(".ttc") else 0

FIRST, LAST = 32, 126
FONT_GLYPHS = LAST - FIRST + 1   # also emitted as a #define; used here for sizing
# px em sizes. The panel is 792x528, so UI text wants to be 20-30px, not 7.
SIZES = [("F_SMALL", 18, REG), ("F_MED", 26, REG), ("F_BIG", 40, BOLD_PATH),
         ("F_HUGE", 64, BOLD_PATH)]
SUIT_PX = 72
SUITS = [("HEART", "\u2665"), ("DIAMOND", "\u2666"), ("CLUB", "\u2663"), ("SPADE", "\u2660")]
THRESH = 128


def render(ch, path, idx, px):
    f = ImageFont.truetype(path, px, index=idx)
    bbox = f.getbbox(ch)
    if not bbox or bbox[2] - bbox[0] <= 0:
        return 0, 0, 0, 0, b""
    x0, y0, x1, y1 = bbox
    PAD = 1
    W, H = (x1 - x0) + 2 * PAD, (y1 - y0) + 2 * PAD
    img = Image.new("L", (W, H), 0)
    ImageDraw.Draw(img).text((PAD - x0, PAD - y0), ch, font=f, fill=255)
    px_data = img.load()
    # stride/rows derived from the SAME dimensions we iterate, so the packer
    # cannot index past the buffer (an earlier version sized from the tight
    # bbox but iterated the padded box and threw IndexError).
    stride = (W + 7) // 8
    rows = (H + 7) // 8
    bm = bytearray(rows * stride)
    for y in range(H):
        for x in range(W):
            if px_data[x, y] >= THRESH:
                bm[(y // 8) * stride + (x // 8)] |= 0x80 >> (x & 7)
    adv = f.getlength(ch)
    return int(round(adv)), W, H, y0 - PAD, bytes(bm)


def build_size(px, path, idx):
    """Return (cellH, stride, meta[], bitmap blob) for one size."""
    glyphs = {}
    for c in range(FIRST, LAST + 1):
        glyphs[c] = render(chr(c), path, idx, px)
    cellW = max((g[1] for g in glyphs.values()), default=1)
    cellH = max((g[2] for g in glyphs.values()), default=1)
    stride = (cellW + 7) // 8          # bytes per ROW
    per = cellH * stride               # bytes per GLYPH: cellH rows, stride each
    # NOTE: a glyph cell is cellH*stride. An earlier version used
    # ceil(cellH/8)*stride, i.e. it divided the row COUNT by 8, which blew the
    # index maths for anything taller than 8px.
    meta, blob = [], bytearray()
    for c in range(FIRST, LAST + 1):
        adv, w, h, top, bm = glyphs[c]
        meta.append((min(adv, 255), w, top, 0))
        cell = bytearray(per)
        srcStride = (w + 7) // 8
        for r in range(min(h, cellH)):
            for b in range(min(srcStride, stride)):
                i = r * srcStride + b
                cell[r * stride + b] = bm[i] if i < len(bm) else 0
        blob += cell
    return cellH, stride, meta, bytes(blob), per


def preview(text, px, path, idx):
    """Render a string to terminal blocks so quality can be judged offline."""
    f = ImageFont.truetype(path, px, index=idx)
    w = int(f.getlength(text)) + 8
    h = px + 14
    img = Image.new("L", (w, h), 255)
    from PIL import ImageDraw
    ImageDraw.Draw(img).text((4, 4), text, font=f, fill=0)
    d = img.load()
    for y in range(h):
        print("   " + "".join(" " if d[x, y] >= THRESH else "#" for x in range(w)))


print("\nPREVIEW (terminal render of what the atlas will contain):\n")
for label, px, path in SIZES[-2:]:
    print(f"  --- {label} @ {px}px, {os.path.basename(path)} ---")
    preview("AKQJ10 1234", px, path, IDX_BOLD if path == BOLD_PATH else IDX_REG)

print("\nSUITS (checking glyph solidity, not just eyeballing 70 rows):")
suit_face = ImageFont.truetype(BOLD_PATH, SUIT_PX, index=IDX_BOLD)
for name, ch in SUITS:
    f = suit_face
    bb = f.getbbox(ch); w, h = bb[2]-bb[0], bb[3]-bb[1]
    img = Image.new("L", (w+2, h+2), 0)
    ImageDraw.Draw(img).text((1-bb[0], 1-bb[1]), ch, font=f, fill=255)
    d = img.load()
    fill = sum(1 for (px_, py_) in [(w//2, h//3), (w//4, h//2), (3*w//4, h//2)]
               if d[px_, py_] >= THRESH)
    print(f"  {name:8} interior {fill}/3  {'SOLID' if fill >= 2 else '*** HOLLOW ***'}")
    if fill < 2:
        raise SystemExit(
            f"suit glyph {name} renders HOLLOW in {os.path.basename(BOLD_PATH)}. "
            "Helvetica Neue, Helvetica, Verdana and Tahoma all ship OUTLINE suit "
            "glyphs - picking a different face rather than drawing voids on the "
            "panel.")
for name, ch in SUITS:
    print(f"  --- {name} @ {SUIT_PX}px ---")
    preview(ch, SUIT_PX, BOLD_PATH, IDX_BOLD)

# ------------------------------------------------------------------ header
out = [
    "// GENERATED by tools/font_gen.py - DO NOT HAND-EDIT.",
    "// Regenerate:  python3 tools/font_gen.py",
    "// Real system faces rasterised to 1bpp for a 259 PPI panel. The old",
    "// hand-drawn 5x7 bitmaps were ~0.5mm tall on this glass; that was the bug.",
    f"// regular face: {os.path.basename(REG)} index {IDX_REG}",
    f"// bold/suit face: {os.path.basename(BOLD_PATH)} index {IDX_BOLD}",
    "// Suits MUST come from the bold face: Helvetica Neue, Helvetica, Verdana",
    "// and Tahoma all ship OUTLINE suit glyphs and render hollow.",
    "// Source faces are macOS system fonts; regenerating elsewhere silently",
    "// substitutes fallbacks, so compare the face lines above after a regen.",
    "#pragma once",
    "#include <stdint.h>",
    "",
    f"#define FONT_FIRST {FIRST}",
    "#define FONT_LAST  126",
    "#define FONT_GLYPHS 95",
    "",
]

sizes_meta = []
for label, px, path in SIZES:
    idx = IDX_BOLD if path == BOLD_PATH else IDX_REG
    cellH, stride, meta, blob, per = build_size(px, path, idx)
    sizes_meta.append((label, px, cellH, stride, per))
    print(f"  {label:8} {px:3}px  cell {stride*8}x{cellH}  pitch {stride}  "
          f"{len(blob)/1024:.1f} KB")

    out.append(f"// ---- {label}  {px}px  cellH={cellH} pitch={stride} bytesPerGlyph={per}")
    out.append(f"static const uint8_t {label}_BM[FONT_GLYPHS][{per}] = {{")
    for i in range(FONT_GLYPHS):
        seg = blob[i * per:(i + 1) * per]
        out.append("  " + ",".join(f"0x{b:02X}" for b in seg) + ",")
    out.append("};")
    out.append(f"static const uint8_t {label}_META[FONT_GLYPHS][4] = {{")
    for m in meta:
        out.append(f"  {{{m[0]},{m[1]},{m[2]},0}},")
    out.append("};")

out.append("")
out.append("enum FontId { " + ", ".join(f"{l} = {i}" for i, (l, *_ ) in enumerate(sizes_meta)) + ", FONT_COUNT };")
out.append("static const uint8_t FONT_CELLH[FONT_COUNT] = {" +
           ",".join(str(s[2]) for s in sizes_meta) + "};")
out.append("static const uint8_t FONT_STRIDE[FONT_COUNT] = {" +
           ",".join(str(s[3]) for s in sizes_meta) + "};")
out.append("static const uint16_t FONT_BYTES[FONT_COUNT] = {" +
           ",".join(str(s[4]) for s in sizes_meta) + "};")
out.append("static const uint8_t* const FONT_BM[FONT_COUNT] = {" +
           ", ".join(f"(const uint8_t*){l}_BM" for l, *_ in sizes_meta) + "};")
out.append("static const uint8_t* const FONT_META[FONT_COUNT] = {" +
           ", ".join(f"(const uint8_t*){l}_META" for l, *_ in sizes_meta) + "};")

# suits
out += ["", "#define SUIT_PX 64", "#define SUIT_STRIDE 8", "enum SuitId { SUIT_HEART = 0, SUIT_DIAMOND, SUIT_CLUB, SUIT_SPADE, SUIT_COUNT };", ""]
out.append("static const uint8_t SUITS[SUIT_COUNT][64*8] = {")
for name, ch in SUITS:
    adv, w, h, top, bm = render(ch, BOLD_PATH, IDX_BOLD, SUIT_PX)
    stride = (w + 7) // 8
    rowB = (h + 7) // 8
    cell = bytearray(64 * 8)
    for r in range(min(h, 64)):
        for b in range(min(stride, 8)):
            i = r * stride + b
            cell[r * 8 + b] = bm[i] if i < len(bm) else 0
    out.append(f"  /* {name} {w}x{h} adv={adv} */")
    for i in range(0, len(cell), 16):
        out.append("  " + ",".join(f"0x{b:02X}" for b in cell[i:i+16]) + ",")
out.append("};")

open(OUT, "w").write("\n".join(out) + "\n")
sz = os.path.getsize(OUT)
print(f"\nwrote {OUT}  ({sz/1024:.0f} KB source)")
