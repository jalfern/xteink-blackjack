#pragma once
#include <stdint.h>
#include <stddef.h>

// Xteink X3 panel: 792x528, 1bpp. Controller is UC8253 (original) or UC8279d
// (newer). This driver implements UC8253 only; panel_begin() logs loudly if the
// panel does not respond, and tools/probe.sh tells you which you have.
namespace epd {

// Orientation. The glass and its RAM are fixed at 792x528 landscape - the gate
// and source drivers are physical. Any other orientation is a software
// transpose applied when pixels are DRAWN; the image sent to the panel is
// always native 792x528. Cheap here only because the X3 has no window RAM, so
// every frame is already full-frame.
constexpr int PHYS_W = 792;
constexpr int PHYS_H = 528;
constexpr int ROW_BYTES = PHYS_W / 8;                  // 99
constexpr size_t FB_SIZE = (size_t)ROW_BYTES * PHYS_H; // 52,272

// 90 = portrait, buttons toward the bottom. This is the shipping value.
// The transpose is proven upright+unmirrored by tools/rotcheck.py, but which
// physical edge of the X3 is "up" cannot be checked from software. If the
// screen appears UPSIDE DOWN, set this to 270 - nothing else needs to change.
// 0 and 180 are landscape (kept for orientation debugging).
#ifndef EPD_ROT
#define EPD_ROT 90
#endif

#if EPD_ROT == 90 || EPD_ROT == 270
constexpr int W = PHYS_H;   // 528
constexpr int H = PHYS_W;   // 792
#else
constexpr int W = PHYS_W;
constexpr int H = PHYS_H;
#endif

// Pins are identical on X3 and X4 (docs/x3-specifications.md).
constexpr int PIN_SCLK = 8;
constexpr int PIN_MOSI = 10;
constexpr int PIN_CS   = 21;
constexpr int PIN_DC   = 4;
constexpr int PIN_RST  = 5;
constexpr int PIN_BUSY = 6;

// 10 MHz. The upstream driver notes that 20 MHz "caused pixel damage in
// testing" on the UC8253, so this is a safety limit, not a tuning knob.
constexpr uint32_t SPI_HZ = 10000000;

void begin();
uint8_t* fb();
void clear(bool white = true);
void pixel(int x, int y, bool white);
// Rect fill in LOGICAL coordinates (portrait-aware, and bulk-writes bytes).
void fillRect(int x, int y, int w, int h, bool white = false);

// Refresh costs. Fast is the workhorse; every one of these is a FULL frame -
// the X3 has no windowed/partial update at all.
void refreshFull();   // ~908 ms LUT + 2x 42 ms RAM  - use for new screens
void refreshFast();   // ~382 ms LUT + 42 ms RAM     - differential
void refreshHalf();   // ~346 ms scrub, kills ghosting without a white flash
void deepSleep();

// Ghosting accumulates on every differential pass, so the game forces a
// periodic clean refresh. Count is frames since the last full/half sync.
extern int g_dirty;
constexpr int DIRTY_LIMIT = 6;
int dirtyCount();

} // namespace epd
