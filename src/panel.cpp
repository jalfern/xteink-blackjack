// Xteink X3 UC8253 driver.
//
// Every register write below is transcribed from the PapyriX driver
// (lib/EInkDisplay/src/Display.cpp, the `_x3Mode` branches) rather than written
// from a generic UC8253 datasheet. That is deliberate: this panel has two
// production controllers with different init scripts, and the upstream author
// documents actual pixel damage from being off-spec. See docs/x3-lut-waveforms.md
// in https://github.com/bigbag/papyrix-reader (MIT).
#include "panel.h"
#include "lut_x3.h"
#include <Arduino.h>
#include <string.h>
#include <SPI.h>

namespace epd {

int g_dirty = 0;
static SPIClass* s_spi = nullptr;
static uint8_t s_fb[FB_SIZE];   // 52 KB, deliberately in internal DRAM for DMA
static bool s_screenOn = false;

// If the panel comes up as a photo-negative, flip this to 0 and rebuild.
// Bit polarity in the UC8253 SRAM is the one detail that cannot be confirmed
// without eyes on real hardware, so it is a switch rather than a guess buried
// in the code.
#ifndef PANEL_INVERT
#define PANEL_INVERT 1
#endif

static inline void csLow()  { digitalWrite(PIN_CS, LOW); }
static inline void csHigh() { digitalWrite(PIN_CS, HIGH); }

static void cmd(uint8_t c) {
  digitalWrite(PIN_DC, LOW);
  csLow();
  s_spi->transfer(c);
  csHigh();
}

static void data(uint8_t d) {
  digitalWrite(PIN_DC, HIGH);
  csLow();
  s_spi->transfer(d);
  csHigh();
}

static void data(const uint8_t* p, size_t n) {
  digitalWrite(PIN_DC, HIGH);
  csLow();
  while (n--) s_spi->transfer(*p++);
  csHigh();
}

static void cmdData(uint8_t c, const uint8_t* p, size_t n) { cmd(c); data(p, n); }
static void cmdData(uint8_t c, uint8_t d) { cmd(c); data(d); }
static void cmdData(uint8_t c, uint8_t a, uint8_t b) { cmd(c); data(a); data(b); }

// X3 reset timing: the long low pulse and trailing 50 ms are X3-specific.
static void resetPanel() {
  digitalWrite(PIN_RST, HIGH); delay(20);
  digitalWrite(PIN_RST, LOW);  delay(10);
  digitalWrite(PIN_RST, HIGH); delay(20);
  delay(50);
}

// X3 BUSY is active LOW. The SSD1677 path in the same upstream driver uses
// active HIGH, which is the classic way to get a "frozen display" bug when
// porting between the two, so this is copied exactly rather than "corrected".
static bool waitBusy(const char* tag) {
  uint32_t t0 = millis();
  bool sawLow = false;
  while (digitalRead(PIN_BUSY) == HIGH) {         // wait for busy to start
    delay(1);
    if (millis() - t0 > 1000) break;
  }
  if (digitalRead(PIN_BUSY) == LOW) {
    sawLow = true;
    while (digitalRead(PIN_BUSY) == LOW) {        // wait for busy to clear
      delay(1);
      if (millis() - t0 > 30000) {
        Serial.printf("EPD: BUSY stuck (%s) - wrong controller? need UC8279d\n", tag ? tag : "");
        return false;
      }
    }
  }
  return sawLow;
}

static void loadLuts(const uint8_t* vcom, const uint8_t* ww, const uint8_t* bw,
                     const uint8_t* wb, const uint8_t* bb) {
  cmdData(0x20, vcom, 42);
  cmdData(0x21, ww, 42);
  cmdData(0x22, bw, 42);
  cmdData(0x23, wb, 42);
  cmdData(0x24, bb, 42);
}

// Send the framebuffer to one RAM plane, Y-mirrored (the X3 gate scan runs the
// other way to our row order, so mirroring is done in software). Note PHYS_H:
// the transpose is applied when pixels are DRAWN, never here - the RAM image
// is always the panel's native 792x528.
static void sendPlane(uint8_t ramCmd) {
  cmd(ramCmd);
  digitalWrite(PIN_DC, HIGH);
  csLow();
  for (int y = 0; y < PHYS_H; y++) {
    const uint8_t* row = s_fb + (size_t)(PHYS_H - 1 - y) * ROW_BYTES;
#if PANEL_INVERT
    for (int i = 0; i < ROW_BYTES; i++) s_spi->transfer((uint8_t)~row[i]);
#else
    s_spi->transferBytes(row, nullptr, ROW_BYTES);
#endif
  }
  csHigh();
}

void begin() {
  pinMode(PIN_CS, OUTPUT);   digitalWrite(PIN_CS, HIGH);
  pinMode(PIN_DC, OUTPUT);
  pinMode(PIN_RST, OUTPUT);
  pinMode(PIN_BUSY, INPUT);

  // Use the core's global SPI object. Do NOT write `new SPIClass(HSPI)`: on
  // this core HSPI/SPI are already SPIClass objects, not host ids, and the
  // ESP32-C3 core does not even define HSPI. Passing the global to the ctor is
  // a compile error that a host-only build will not show you.
  s_spi = &SPI;
  s_spi->begin(PIN_SCLK, -1, PIN_MOSI, PIN_CS);

  resetPanel();

  // --- UC8253 init, transcribed from Display.cpp initDisplayController() ---
  cmdData(0x00, (uint8_t)0x3F, (uint8_t)0x08);
  // Resolution register is programmed 792 x 600, not 792 x 528: the panel has
  // more gates than visible rows. Sending 528 here gives a shifted/blank image.
  cmd(0x61); data(0x03); data(0x18); data(0x02); data(0x58);
  cmd(0x65); data(0x00); data(0x00); data(0x00); data(0x00);
  cmdData(0x03, (uint8_t)0x1D);
  cmd(0x01); data(0x07); data(0x17); data(0x3F); data(0x3F); data(0x17);
  cmdData(0x82, (uint8_t)0x1D);
  cmd(0x06); data(0x25); data(0x25); data(0x3C); data(0x37);
  cmdData(0x30, (uint8_t)0x09);   // PLL: ~18.2 ms per frame group
  cmdData(0xE1, (uint8_t)0x02);
  loadLuts(lut_x3_vcom_full, lut_x3_ww_full, lut_x3_bw_full,
           lut_x3_wb_full, lut_x3_bb_full);
  s_screenOn = false;
  Serial.println("EPD: UC8253 initialised");
}

uint8_t* fb() { return s_fb; }

void clear(bool white) {
  memset(s_fb, white ? 0xFF : 0x00, FB_SIZE);
  g_dirty = 0;
}

void pixel(int x, int y, bool white) {
  if ((unsigned)x >= (unsigned)W || (unsigned)y >= (unsigned)H) return;
#if EPD_ROT == 90
  // Portrait, upright. Note the x inversion: sendPlane() already flips rows,
  // and without folding that in here the result comes out left-right mirrored.
  // tools/rotcheck.py is what proved this - the naive (H-1-y, x) mapping was
  // mirrored, and reading symmetric letters in an ASCII dump hid it.
  const int px = H - 1 - y, py = W - 1 - x;
#elif EPD_ROT == 270
  const int px = y, py = x;                        // portrait, upside down
#elif EPD_ROT == 180
  const int px = PHYS_W - 1 - x, py = y;
#else
  const int px = x, py = PHYS_H - 1 - y;           // landscape
#endif
  uint8_t* p = s_fb + (size_t)py * ROW_BYTES + (px >> 3);
  uint8_t m = (uint8_t)(0x80 >> (px & 7));
  *p = white ? (uint8_t)(*p | m) : (uint8_t)(*p & ~m);
}

// Fast rectangle, ROT-90 only (the shipping orientation). Under this transpose
// a logical rectangle maps to a physical rectangle whose rows all share the
// SAME bit range, because physical x depends only on logical y - so each row
// is one masked byte run. Other orientations fall back to per-pixel, which is
// correct but slower; they exist for orientation diagnosis, not play.
void fillRect(int x, int y, int w, int h, bool white) {
  if (w <= 0 || h <= 0) return;
#if EPD_ROT == 90
  int py1 = W - 1 - x, py0 = W - 1 - (x + w - 1);
  int px1 = H - 1 - y;
  int px0 = H - 1 - (y + h - 1);
  if (px0 < 0) px0 = 0;
  if (py0 < 0) py0 = 0;
  if (px1 > PHYS_W - 1) px1 = PHYS_W - 1;
  if (py1 > PHYS_H - 1) py1 = PHYS_H - 1;
  if (px0 > px1 || py0 > py1) return;

  const int b0 = px0 >> 3, b1 = px1 >> 3;
  const uint8_t m0 = (uint8_t)(0xFF >> (px0 & 7));
  const uint8_t m1 = (uint8_t)(0xFF << (px1 & 7));
  for (int py = py0; py <= py1; py++) {
    uint8_t* row = s_fb + (size_t)py * ROW_BYTES;
    if (b0 == b1) {
      uint8_t m = (uint8_t)(m0 & m1);
      row[b0] = white ? (uint8_t)(row[b0] & ~m) : (uint8_t)(row[b0] | m);
    } else {
      row[b0] = white ? (uint8_t)(row[b0] & ~m0) : (uint8_t)(row[b0] | m0);
      for (int b = b0 + 1; b < b1; b++) row[b] = white ? 0x00 : 0xFF;
      row[b1] = white ? (uint8_t)(row[b1] & ~m1) : (uint8_t)(row[b1] | m1);
    }
  }
#else
  for (int j = 0; j < h; j++)
    for (int i = 0; i < w; i++) pixel(x + i, y + j, white);
#endif
}

static void powerAndRefresh(const char* tag) {
  if (!s_screenOn) { cmd(0x04); s_screenOn = true; }  // booster/charge pump
  cmd(0x12);                                          // trigger
  waitBusy(tag);
}

// Full sync: img LUTs, the frame written to BOTH RAMs (inverted), CDI 0xA9/0x07.
// After this the controller's old/new planes match, so the next differential is clean.
void refreshFull() {
  loadLuts(lut_x3_vcom_img, lut_x3_ww_img, lut_x3_bw_img,
           lut_x3_wb_img, lut_x3_bb_img);
  sendPlane(0x13);
  sendPlane(0x10);
  cmdData(0x50, (uint8_t)0xA9, (uint8_t)0x07);
  powerAndRefresh("full");
  g_dirty = 0;
}

// Differential: turbo LUTs, only RAM 0x13, CDI 0x29/0x07 (border held).
// The controller diffs 0x13 against 0x10, so 0x10 must then be brought up to
// the frame we just showed or the next diff is computed against stale data.
void refreshFast() {
  loadLuts(lut_x3_vcom_turbo, lut_x3_ww_turbo, lut_x3_bw_turbo,
           lut_x3_wb_turbo, lut_x3_bb_turbo);
  sendPlane(0x13);
  cmdData(0x50, (uint8_t)0x29, (uint8_t)0x07);
  powerAndRefresh("fast");
  sendPlane(0x10);
  g_dirty++;
}

// Scrub: half LUTs, new frame only, no white flash. Clears accumulated ghost.
void refreshHalf() {
  loadLuts(lut_x3_vcom_half, lut_x3_ww_half, lut_x3_bw_half,
           lut_x3_wb_half, lut_x3_bb_half);
  sendPlane(0x13);
  cmdData(0x50, (uint8_t)0xA9, (uint8_t)0x07);
  powerAndRefresh("half");
  sendPlane(0x10);
  g_dirty = 0;
}

void deepSleep() {
  cmd(0x02);
  cmd(0x10);
  s_screenOn = false;
  delay(100);
}

} // namespace epd
