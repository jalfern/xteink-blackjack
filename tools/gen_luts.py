#!/usr/bin/env python3
"""Generate src/lut_x3.h from the PapyriX driver source.

LUT bytes are waveform/voltage tables. Getting one wrong applies the wrong
drive voltage to the panel for hundreds of milliseconds. Upstream is blunt
about it (papyrix-reader docs/x3-lut-waveforms.md): "The UC8253 X3 uses 10 MHz
SPI (20 MHz caused pixel damage in testing)." So they are never hand-typed:
they are scraped, validated, and regenerated.

Rules enforced here:
  * exactly 42 bytes per LUT (7 phases x 6 bytes), or the LUT is rejected
  * every token must be a bare 0xNN hex literal (no comments/macros inside)
  * a waveform set must have all five planes (VCOM/WW/BW/WB/BB) or the whole
    set is dropped - a half-present set is worse than an absent one, because it
    looks usable. (This is why the grayscale set is excluded: four of its five
    planes do not scrape cleanly, and this build is 1-bit only anyway.)
"""
import os, re, sys, urllib.request

UPSTREAM = ("https://raw.githubusercontent.com/bigbag/papyrix-reader/main/"
            "lib/EInkDisplay/src/Display.cpp")
PLANES = ("vcom", "ww", "bw", "wb", "bb")
HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
CACHE = "/tmp/x3ref/Display.cpp"
HEX = re.compile(r"0x[0-9A-Fa-f]{2}$")


def source():
    if os.path.exists(CACHE) and os.path.getsize(CACHE) > 40000:
        return open(CACHE, encoding="utf-8", errors="replace").read()
    req = urllib.request.Request(UPSTREAM, headers={"User-Agent": "xteink-blackjack"})
    data = urllib.request.urlopen(req, timeout=30).read().decode("utf-8", "replace")
    os.makedirs(os.path.dirname(CACHE), exist_ok=True)
    open(CACHE, "w", encoding="utf-8").write(data)
    return data


def scrape(text):
    good, rejected = {}, []
    for m in re.finditer(r"const uint8_t (lut_x3_(\w+))\[\]\s+PROGMEM\s*=\s*\{(.*?)\};", text, re.S):
        name, _, body = m.groups()
        vals = [v.strip() for v in body.replace("\n", " ").split(",") if v.strip()]
        if not all(HEX.match(v) for v in vals):
            rejected.append((name, f"{len(vals)} tokens, non-literal content"))
        elif len(vals) != 42:
            rejected.append((name, f"{len(vals)} bytes, expected 42"))
        else:
            good[name] = vals
    return good, rejected


def main():
    text = source()
    good, rejected = scrape(text)

    # group lut_x3_<plane>_<set> and keep only complete sets
    sets = {}
    for name, vals in good.items():
        plane, _, kind = name[len("lut_x3_"):].rpartition("_")
        sets.setdefault(kind, {})[plane] = vals

    kept = {}
    for kind, planes in sorted(sets.items()):
        missing = [p for p in PLANES if p not in planes]
        if missing:
            rejected.append((f"{kind} set", "missing planes: " + ",".join(missing)))
            continue
        for p in PLANES:
            kept[f"lut_x3_{p}_{kind}"] = planes[p]

    out = [
        "// X3 UC8253 LUT waveforms - extracted verbatim from PapyriX",
        "// lib/EInkDisplay/src/Display.cpp",
        "// Copyright (c) PapyriX contributors, MIT License",
        "// https://github.com/bigbag/papyrix-reader",
        "//",
        "// DO NOT HAND-EDIT. Regenerate with:  python3 tools/gen_luts.py",
        "// Every table below is validated to exactly 42 hex literals.",
        "",
        "#pragma once",
        "#include <stdint.h>",
        "",
    ]
    for name in sorted(kept):
        out.append(f"static const uint8_t {name}[42] = {{{', '.join(kept[name])}}};")
    dest = os.path.join(ROOT, "src", "lut_x3.h")
    open(dest, "w").write("\n".join(out) + "\n")

    for n, why in rejected:
        print(f"  rejected {n:24} {why}")
    kinds = sorted({n.rpartition('_')[2] for n in kept})
    print(f"\nwrote {dest}\n  {len(kept)} LUTs in {len(kinds)} sets: {', '.join(kinds)}")
    if not kept:
        sys.exit("no usable LUTs scraped - refusing to write an empty header")


if __name__ == "__main__":
    main()
