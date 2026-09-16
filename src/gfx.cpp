#include "gfx.h"
#include "panel.h"
#include <string.h>

namespace gfx {

void px(int x, int y, bool white) { epd::pixel(x, y, white); }

void hline(int x, int y, int w, bool white) {
  for (int i = 0; i < w; i++) px(x + i, y, white);
}
void vline(int x, int y, int h, bool white) {
  for (int i = 0; i < h; i++) px(x, y + i, white);
}
void fill(int x, int y, int w, int h, bool white) { epd::fillRect(x, y, w, h, white); }
void rect(int x, int y, int w, int h, bool white) { fill(x, y, w, h, white); }
void frame(int x, int y, int w, int h, bool white) {
  hline(x, y, w, white);
  hline(x, y + h - 1, w, white);
  vline(x, y, h, white);
  vline(x + w - 1, y, h, white);
}

int fontH(int font) {
  if (font < 0 || font >= FONT_COUNT) font = F_MED;
  return FONT_CELLH[font];
}

static inline int clampFont(int f) {
  return (f < 0 || f >= FONT_COUNT) ? F_MED : f;
}

int textW(const char* s, int font) {
  font = clampFont(font);
  const uint8_t* meta = FONT_META[font];
  int w = 0;
  for (; *s; s++) {
    unsigned char c = (unsigned char)*s;
    if (c < FONT_FIRST || c > FONT_LAST) c = '?';
    w += meta[(c - FONT_FIRST) * 4];      // advance, from the typeface
  }
  return w;
}

void text(int x, int y, const char* s, int font, bool white) {
  font = clampFont(font);
  const int pitch = FONT_STRIDE[font];
  const int cellH = FONT_CELLH[font];
  const int per = FONT_BYTES[font];
  const uint8_t* bm = FONT_BM[font];
  const uint8_t* meta = FONT_META[font];
  int cx = x;
  for (; *s; s++) {
    unsigned char c = (unsigned char)*s;
    if (c < FONT_FIRST || c > FONT_LAST) c = '?';
    int g = c - FONT_FIRST;
    const uint8_t* gmeta = meta + g * 4;
    int adv = gmeta[0], inkW = gmeta[1], top = gmeta[2];
    const uint8_t* gb = bm + (size_t)g * per;
    int gy = y + top;
    for (int row = 0; row < cellH; row++) {
      const uint8_t* rb = gb + (size_t)row * pitch;
      for (int col = 0; col < inkW; col++) {
        if (rb[col >> 3] & (0x80 >> (col & 7))) px(cx + col, gy + row, white);
      }
    }
    cx += adv;                              // proportional advance
  }
}

void textC(int yy, const char* s, int font, bool white) {
  text((epd::W - textW(s, font)) / 2, yy, s, font, white);
}
void textR(int x, int y, const char* s, int font, bool white) {
  text(x - textW(s, font), y, s, font, white);
}
void textCX(int cx, int cy, const char* s, int font, bool white) {
  text(cx - textW(s, font) / 2, cy - FONT_CELLH[clampFont(font)] / 2, s, font, white);
}

void suit(int x, int y, int id, bool white) {
  if (id < 0 || id >= SUIT_COUNT) return;
  const uint8_t* g = SUITS[id];
  for (int row = 0; row < SUIT_PX; row++) {
    const uint8_t* rb = g + (size_t)row * SUIT_STRIDE;
    for (int col = 0; col < SUIT_STRIDE * 8; col++)
      if (rb[col >> 3] & (0x80 >> (col & 7))) px(x + col, y + row, white);
  }
}

void card(int x, int y, int w, int h, const char* rank, int id, bool faceDown) {
  fill(x, y, w, h, true);
  frame(x, y, w, h, false);
  frame(x + 2, y + 2, w - 4, h - 4, false);

  if (faceDown) {
    for (int j = 10; j < h - 10; j += 6)
      for (int i = 10; i < w - 10; i += 6)
        px(x + i, y + j, false);
    frame(x + 7, y + 7, w - 14, h - 14, false);
    return;
  }

  const int pad = 9;
  text(x + pad, y + pad - 4, rank, F_MED, false);

  if (id >= 0 && id < SUIT_COUNT) {
    int sx = x + (w - SUIT_PX) / 2;
    int sy = y + (h - SUIT_PX) / 2 + 4;
    suit(sx, sy, id, false);
  }

  // bottom-right index, reading like an inverted playing card
  int rw = textW(rank, F_MED);
  text(x + w - pad - rw, y + h - pad - fontH(F_MED) + 4, rank, F_MED, false);
}

void menuItem(int x, int y, int w, const char* label, bool sel, int font) {
  const int h = fontH(font) + 26;
  if (sel) {
    fill(x, y, w, h, false);
    text(x + 22, y + 12, label, font, true);
  } else {
    fill(x, y, w, h, true);
    text(x + 22, y + 12, label, font, false);
  }
  frame(x, y, w, h, false);
}

} // namespace gfx
