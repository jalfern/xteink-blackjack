#pragma once
#include <stdint.h>
#include "gfx_font.h"

namespace gfx {

// Drawing writes into the panel framebuffer only; the caller decides when to
// pay for a refresh, because a refresh costs ~400 ms on this panel.
void px(int x, int y, bool white = false);
void hline(int x, int y, int w, bool white = false);
void vline(int x, int y, int h, bool white = false);
void fill(int x, int y, int w, int h, bool white = false);
void rect(int x, int y, int w, int h, bool white = false);
void frame(int x, int y, int w, int h, bool white = false);

// `font` is a FontId from gfx_font.h (F_SMALL/F_MED/F_BIG/F_HUGE). Glyphs are
// proportional - advance comes from the typeface, not a fixed cell.
int textW(const char* s, int font = F_MED);
// y is the TOP of the glyph cell.
void text(int x, int y, const char* s, int font = F_MED, bool white = false);
void textC(int y, const char* s, int font = F_MED, bool white = false);
void textR(int x, int y, const char* s, int font = F_MED, bool white = false);
// Centred on both axes inside a box - used for card faces.
void textCX(int cx, int cy, const char* s, int font = F_MED, bool white = false);

// 64x64 suit glyph, top-left at x,y.
void suit(int x, int y, int suitId, bool white = false);

void card(int x, int y, int w, int h, const char* rank, int suitId, bool faceDown);
void menuItem(int x, int y, int w, const char* label, bool selected, int font = F_BIG);

// Vertical centre of a font's cell, handy for aligning mixed sizes.
int fontH(int font);

} // namespace gfx
