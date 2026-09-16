#!/usr/bin/env bash
# Host-side checks. Everything that can be proven without hardware:
# LUT extraction integrity, font glyph sanity, and the rules engine.
set -uo pipefail
cd "$(dirname "$0")/.."
fail=0

echo "== 1. LUT extraction =="
if python3 tools/gen_luts.py; then :; else echo "  LUT GEN FAILED"; fail=1; fi

echo
echo "== 2. Font =="
if python3 tools/font_gen.py >/tmp/font_preview.txt 2>&1; then
  echo "  ok (full preview: /tmp/font_preview.txt)"
else
  echo "  FONT GEN FAILED"; sed -n '1,5p' /tmp/font_preview.txt; fail=1
fi

echo
echo "== 3. Rules engine (800k+ checks) =="
if clang++ -std=c++17 -I src -Wall -Wextra tools/logic_test.cpp -o /tmp/bjtest 2>/tmp/cc.err; then
  /tmp/bjtest || fail=1
else
  echo "  COMPILE FAILED"; cat /tmp/cc.err; fail=1
fi

echo
echo "== 4. Orientation mapping (portrait transpose round-trip) =="
# Proves the logical->physical mapping is upright and unmirrored, and that
# panel.cpp and tools/rotcheck.py still agree. See rotcheck.py for why an
# eyeball check of an ASCII dump is not sufficient evidence.
for r in 90 270 0 180; do
  printf "    EPD_ROT=%-4s " "$r"
  if python3 tools/rotcheck.py "$r" --quiet >/tmp/rot.$r 2>&1; then
    grep -E "PASS" /tmp/rot.$r | sed 's/^ *//'
  else
    echo "FAILED"; tail -4 /tmp/rot.$r; fail=1
  fi
done

echo
echo "== 5. Panel/input/gfx/main compile check =="
# Every EPD_ROT branch is compiled, not just the shipped one - a mapping that
# only ever gets built at 90 will rot in the other three silently.
for f in src/panel.cpp src/input.cpp src/gfx.cpp src/main.cpp; do
  printf "    %-14s " "$(basename $f)"
  bad=""
  for r in 0 90 180 270; do
    clang++ -std=c++17 -fsyntax-only -Wall -DEPD_ROT=$r -I tools/stub -I src "$f" 2>/tmp/tu.err || bad="$bad $r"
  done
  if [ -n "$bad" ]; then echo "FAILED at EPD_ROT:$bad"; head -6 /tmp/tu.err; fail=1; else echo "ok (rot 0/90/180/270)"; fi
done

echo
[ $fail -eq 0 ] && echo "ALL HOST CHECKS PASSED" || echo "FAILURES - do not flash yet"
exit $fail
