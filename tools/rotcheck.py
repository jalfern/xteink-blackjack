#!/usr/bin/env python3
"""Prove the orientation mapping before touching hardware.

Usage:  python3 tools/rotcheck.py [ROT] [--quiet]      ROT default 90

Reproduces panel.cpp exactly:
  pixel():     logical -> physical  (the table under test)
  sendPlane(): panel row r <- fb row PHYS_H-1-r   (the driver's Y mirror)

Then reconstructs what a human would see holding the device, and requires it to
reproduce the original drawing PIXEL-FOR-PIXEL. On mismatch it searches all
eight dihedral transforms and names the actual composition, so the failure says
"this is mirrored" / "this is rotated 180" rather than just being red.

WHY THIS EXISTS. The first portrait mapping I wrote, (H-1-y, x), was left-right
mirrored. I checked an ASCII dump of it, saw "A" and "X" and pronounced it
upright - but A and X are symmetric, so a mirror is invisible in them. At that
resolution a mirrored '3' also passed for a '3'. An eyeball test of a rendering
you already believe is correct confirms your expectation; only a round-trip
comparison against the source drawing can falsify it.

The mapping table here must stay in lockstep with pixel() in panel.cpp - and it
is CHECKED against it: the C expressions for this rotation are scraped out of
panel.cpp and evaluated in Python, then compared to to_phys() over random
points. If someone edits one file and not the other, this script fails.
"""
from PIL import Image, ImageDraw, ImageFont, ImageChops
import sys

ROT = int(sys.argv[1]) if len(sys.argv) > 1 and sys.argv[1].isdigit() else 90
QUIET = "--quiet" in sys.argv

PHYS_W, PHYS_H = 792, 528                    # panel RAM, fixed by the drivers
LOG_W, LOG_H = (PHYS_H, PHYS_W) if ROT in (90, 270) else (PHYS_W, PHYS_H)


def to_phys(lx, ly):
    """Mirror of pixel() in panel.cpp - verified against it by c_mapping_below."""
    if ROT == 90:   return LOG_H - 1 - ly, LOG_W - 1 - lx
    if ROT == 270:  return ly, lx
    if ROT == 180:  return PHYS_W - 1 - lx, ly
    return lx, PHYS_H - 1 - ly


# --------------------------------------------------- scrape the C, don't trust
import os, random, re, sys

CPP = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "src", "panel.cpp")


def c_mapping():
    """Evaluate the real pixel() arithmetic for this ROT.

    The C preprocessor picks the active branch (clang -E -DEPD_ROT=N), so this
    cannot disagree with the compiler about which #if fired, and there is no
    hand-written conditional parser here to rot.
    """
    import subprocess
    root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    pp = subprocess.run(
        ["clang++", "-E", "-P", "-x", "c++", f"-DEPD_ROT={ROT}",
         "-I", os.path.join(root, "tools", "stub"), "-I", os.path.join(root, "src"),
         os.path.join(root, "src", "panel.cpp")],
        capture_output=True, text=True, cwd=root)
    if pp.returncode != 0:
        raise SystemExit("preprocessing panel.cpp failed:\n" + pp.stderr[:800])
    m = re.search(r"const int px\s*=\s*([^,;]+),\s*py\s*=\s*([^;]+);", pp.stdout)
    if not m:
        raise SystemExit(f"no px/py mapping survived preprocessing for ROT={ROT}")
    ex, ey = m.group(1).strip(), m.group(2).strip()
    env = {"H": LOG_H, "W": LOG_W, "PHYS_W": PHYS_W, "PHYS_H": PHYS_H}
    rnd = random.Random(7)
    for _ in range(4000):
        x, y = rnd.randrange(LOG_W), rnd.randrange(LOG_H)
        e = dict(env, x=x, y=y)
        want = (eval(ex, {}, e), eval(ey, {}, e))
        got = to_phys(x, y)
        if want != got:
            raise SystemExit(f"panel.cpp and rotcheck DISAGREE at ({x},{y}): "
                             f"C says {want}, python says {got}")
    print(f"  C/python mapping agree for EPD_ROT={ROT}:  px={ex}   py={ey}")


c_mapping()


fb = bytearray(PHYS_H * (PHYS_W // 8))


def put(px, py, dark):
    b = py * (PHYS_W // 8) + (px >> 3)
    m = 0x80 >> (px & 7)
    fb[b] = (fb[b] | m) if dark else (fb[b] & ~m)


# Asymmetric test string on purpose. A/X/8/0/H/O are symmetric under mirroring
# and are useless for detecting a flip; use g/3/7/e/R and a lopsided frame.
img = Image.new("1", (LOG_W, LOG_H), 1)
d = ImageDraw.Draw(img)
try:
    f = ImageFont.truetype("/System/Library/Fonts/Supplemental/Arial Bold.ttf", 90)
except OSError:
    f = ImageFont.load_default()
d.text((30, 40), "Rg 37", font=f, fill=0)
d.polygon([(30, 200), (420, 240), (60, 330)], fill=0)      # lopsided marker
d.rectangle([20, 30, 440, 350], outline=0, width=4)

for ly in range(LOG_H):
    for lx in range(LOG_W):
        if img.getpixel((lx, ly)) == 0:
            put(*to_phys(lx, ly), True)

# The driver mirrors rows on the way out; the displayed image is not the fb.
disp = Image.new("1", (PHYS_W, PHYS_H), 1)
dp = disp.load()
for r in range(PHYS_H):
    src = (PHYS_H - 1 - r) * (PHYS_W // 8)
    for by in range(PHYS_W // 8):
        v = fb[src + by]
        for i in range(8):
            dp[by * 8 + i, r] = 0 if (v >> (7 - i)) & 1 else 1

# Reconstruct the human's point of view: portrait means the glass is turned
# clockwise from the panel's native landscape, so undo that with a CCW rotate.
view = disp.rotate(90, expand=True) if ROT in (90, 270) else disp
print(f"EPD_ROT={ROT}  panel RAM {PHYS_W}x{PHYS_H}  as held: {view.size}")

if not QUIET:
    S = 8
    print("\nwhat the screen shows, held upright:\n")
    for y in range(0, view.size[1], S):
        row = ""
        for x in range(0, view.size[0], S // 2):
            blk = [view.getpixel((min(x + i, view.size[0] - 1), min(y + j, view.size[1] - 1)))
                   for i in range(S // 2) for j in range(S)]
            row += "#" if blk.count(0) > len(blk) * 0.28 else "."
        print(row)

# ------------------------------------------------------------------- verdict
# Each rotation has an expected result. 90 and 0 must reproduce the drawing
# exactly; 270 and 180 are DEFINED as the upside-down variants, so for them a
# perfect 180 rotation is the pass condition. Without this, a broken 270 would
# look identical to a working one.
EXPECT = {90: "identity (upright, correct)", 0: "identity (upright, correct)",
          270: "rotated 180", 180: "rotated 180"}

ref = img.convert("L")
vt = view.convert("L")

DIHEDRAL = {
    "identity (upright, correct)": lambda i: i,
    "left-right MIRRORED":         lambda i: i.transpose(Image.FLIP_LEFT_RIGHT),
    "top-bottom MIRRORED":         lambda i: i.transpose(Image.FLIP_TOP_BOTTOM),
    "rotated 180":                 lambda i: i.rotate(180),
    "transposed (mirror+rotate)":  lambda i: i.transpose(Image.TRANSPOSE),
    "transverse (mirror+rotate)":  lambda i: i.transpose(Image.TRANSVERSE),
}


def diff(a, b):
    """Count of differing pixels (255=black vs 0=white in mode 'L'), or None."""
    if a.size != b.size:
        return None
    return sum(1 for p in ImageChops.difference(a, b).tobytes() if p)


print(f"\nround-trip: {diff(ref, vt)} of {view.size[0]*view.size[1]} bytes differ")

found = None
for name, fn in DIHEDRAL.items():
    n = diff(fn(ref), vt)
    if n is not None and n == 0:
        found = name
        break

want = EXPECT.get(ROT, "identity (upright, correct)")
if found == want:
    print(f"  PASS - renders as '{found}', which is what EPD_ROT={ROT} promises")
    sys.exit(0)
print(f"  FAIL - EPD_ROT={ROT} promises '{want}' but renders '{found}'")
sys.exit(1)
