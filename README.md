# X3 Blackjack

Blackjack for the **Xteink X3** pocket e-reader. Standalone ESP32-C3 firmware that
lives in the `app1` slot next to CrossPoint; the reader stays the default boot.

```
tools/test.sh      host checks: LUTs, font, orientation, 800k+ rules assertions
tools/flash.sh     build + write app1 only, then verify by re-reading flash
tools/play.sh      boot the game from CrossPoint (needs USB - see its header)
tools/switch.sh    pick which firmware boots (reader | game | capture | status)
tools/rotcheck.py  prove the portrait mapping is upright, without hardware
```

## Status

**Running on hardware, portrait.** Current boot log:

```
X3 Blackjack - boot (EPD_ROT=90, logical 528x792)
BOOT: running from 'app1' @0x780000 size=7798784
BOOT: 'back to reader' selects 'app0' @0x10000
EPD: UC8253 initialised
```

Verified on the real device:

- partition table read off flash matches `partitions.csv` entry for entry
- **panel is UC8253**, so this driver is the right one and no UC8279d port is needed
- CrossPoint in `app0` is byte-identical to `crosspoint-1.6.0-x3-x4.bin` (header
  `e9 07 02 4f 88 09 38 40`) - the game did not disturb the reader
- flashing app1 + reading it back: digest matched
- otadata bootstrap solved (below); switching slots is a 32-byte write
- stable boot, no panic, no reset loop
- **frame polarity is correct** - dark on white, so `PANEL_INVERT` needs no flip
- **the POWER gesture works on real hardware**, proven from flash rather than
  assumed: a long hold returned the device to CrossPoint, and reading otadata
  afterwards showed copy#0 at `seq=5` where it had been left at `3`. Only
  `esp_ota_set_boot_partition()` bumps that, so the write definitely happened
- GPIO3 genuinely sees POWER, which was in real doubt (it could have been a
  power latch with no readable input, in which case this gesture is impossible)
- rules engine - **801,291 assertions, 0 failures**, incl. a 200,000-hand soak
- statistical shape correct for a naive hit-to-17 bot: blackjack **4.49%**
  (real ~4.5%), push **9.87%** (~9%), player bust **26.9%**
- build: RAM 20.7%, Flash 4.5% (354 KB of a 7.6 MB slot; font atlas ~97 KB)
- builds are **byte-reproducible**: two clean rebuilds of unchanged source give
  identical binaries. That is what caught a stale `app1` - a fresh build differed
  from the flashed image in 64 scattered bytes, so the device was NOT running
  committed source; it was reflashed rather than assumed current

Still to confirm by eye, on screen:

1. **Physical key order.** KEY TEST now lists all seven keys including POWER and
   highlights the one pressed. The six ADC buckets are transcribed from PapyriX
   and that project's button layout may not match this unit. POWER is confirmed;
   the ADC keys are not.
2. **Real refresh timings** - PANEL TEST prints ms for fast/half/full so the
   upstream figures below can be replaced with numbers from this unit.
3. **Whether the type actually reads well.** Weight, spacing and card layout at
   259 PPI are judgement calls made blind so far. Iterating this needs panel
   photos fed back to the agent - the next harness step, and the reason work is
   paused here.

## Orientation: portrait

The panel RAM is **792x528 landscape, fixed** - the gate and source drivers are
physical. Portrait is a software transpose applied in `pixel()`; the bytes sent
to the panel are always native landscape. That is cheap here only because the X3
has no window RAM, so a frame is full-frame anyway.

`EPD_ROT` in `panel.h` selects it, and **every branch is compiled by
`test.sh`** - a mapping that is only ever built at 90 rots in the other three:

| `EPD_ROT` | result |
|---|---|
| **90** | **portrait, upright, unmirrored - shipping** |
| 270 | portrait, upside down |
| 0 | landscape, upright |
| 180 | landscape, upside down |

If the screen ever appears upside down, change one number to `270`.

`tools/rotcheck.py` proves the mapping without hardware: it draws an asymmetric
target, maps it through the **real C expressions** (obtained by running the
preprocessor over `panel.cpp`, so the test cannot silently drift from the
driver), applies the driver's row mirror, reconstructs what an eye would see,
and requires a **pixel-exact** match. Each rotation is asserted against its own
expectation, so a broken 270 cannot masquerade as a working one.

**The first mapping I wrote was wrong and my own visual check approved it.**
`(H-1-y, x)` was left-right mirrored. I dumped the result as ASCII, saw shapes I
read as "A", "X" and "3", and called it upright - but **A and X are symmetric**,
so a mirror is invisible in them, and at that sampling rate a reversed 3 passes
for a 3. Looking at a rendering you already believe is correct only confirms
your expectation; only a round-trip comparison against the source drawing can
falsify it. `rotcheck` now draws `Rg 37` plus a lopsided triangle precisely
because those cannot survive an unnoticed flip.

## Type

The original font was hand-drawn 5x7 bitmaps. On 259 PPI glass those glyphs are
about **half a millimetre tall** - a size appropriate to a 128x64 character LCD,
which is the class of display this project is not. Wrong size class, not sloppy
drawing.

`tools/font_gen.py` now rasterises **real system faces** with Pillow:

| id | size | face |
|---|---|---|
| `F_SMALL` | 18 px | Helvetica Neue |
| `F_MED` | 26 px | Helvetica Neue |
| `F_BIG` | 40 px | Arial Bold |
| `F_HUGE` | 64 px | Arial Bold |

Advances are proportional, taken from `font.getlength()` rather than a fixed
cell, so `1` and `W` differ and text stops looking typewriter-ish. The chosen
face is recorded in the generated header, because regenerating on a machine
without these fonts substitutes fallbacks **silently**.

One trap worth keeping: **Helvetica Neue, Helvetica, Verdana and Tahoma all ship
OUTLINE suit glyphs.** A heart rasterised from them is a hollow outline, and it
reads like a threshold bug rather than a missing fill. The generator now
counts interior pixels per suit and fails the build:

```
HEART  interior 3/3  SOLID
```

Suits are therefore pinned to Arial Bold, permanently.

## Why a firmware and not an "app"

There is no app runtime on this device. CrossPoint's web API is files, settings
and fonts; the stock firmware's web UI is an unauthenticated filesystem. No
plugins, no bytecode loader, no dynamic linking on ESP-IDF. So: firmware.

The reason that isn't scary is blast radius. This project writes **one 7.6 MB app
slot**. Bootloader, partition table and `nvs` are never touched — `flash.sh` has
no code path that addresses them.

## Safety model

```
app0   0x00010000  CrossPoint 1.6.0        <- default boot, never written here
app1   0x00780000  this game               <- the only thing flash.sh writes
otadata 0x0000E000  which one boots
```

- `partitions.csv` is byte-identical to stock geometry. Writing a partition table
  is the one operation here that could look like a brick; we don't do it.
- **Blank or corrupt otadata boots `ota_0`.** So "go back to reader" is
  unbreakable — `switch.sh reader` just erases 32 bytes and CrossPoint comes up.
- Full stock 16 MB dump exists on two physical disks
  (`~/XteinkX3-backup/`, `/Volumes/OWC Express 1M2/...`). `flash.sh` refuses to
  run if that backup is missing.
- Long-press POWER ~1.5 s inside the game calls `esp_ota_set_boot_partition(app0)`
  and reboots. Correct by construction, no hand-written bytes, no cable.

Note `app1` previously held stock XT V6.3.15. Installing the game replaces it.
The stock image remains re-flashable at `0x780000` from the backups.

## Panel reality, which is why the game is turn-based

The X3 **cannot do partial refresh**: *"Both X3 controllers use a full-frame fast
refresh for a window request. The UC8253 has no window RAM commands."* Every
update pushes all 52,272 bytes.

| operation | upstream figure | what it means |
|---|---|---|
| SPI frame push | ~42 ms @ 10 MHz | 20 MHz caused **pixel damage**, so this is a ceiling |
| turbo LUT drive | ~382 ms | the fast path |
| img LUT drive | ~908 ms | clean refresh, use for new screens |
| **fast refresh total** | **~425 ms → ~2.4 fps** | hard floor, not a tuning target |

That is why Blackjack fits and Breakout does not. 425 ms reads as *turning a
page*, which is the native idiom of this device. It would read as a slideshow
for anything animated. Ghosting also accumulates on every differential pass, so
`showFast()` promotes to a half-refresh scrub every `DIRTY_LIMIT` (6) frames.

## Buttons — six keys on two ADC pins

Not GPIO switches. Six keys share two ADC pins through a resistor ladder, so you
read a **voltage** to know which key is down. Values measured on real devices
(from PapyriX, which publishes its samples):

| key | pin | ADC | bucket (low, high] |
|---|---|---|---|
| Back | GPIO1 | ~3512 | 3100–3900 |
| Confirm | GPIO1 | ~2694 | 2090–3100 |
| Left | GPIO1 | ~1493 | 750–2090 |
| Right | GPIO1 | ~5 | ≤750 |
| Up | GPIO2 | ~2242 | 1120–3900 |
| Down | GPIO2 | ~5 | ≤1120 |
| Power | GPIO3 | digital | active LOW |

Idle is pulled to ~4095, above every bucket, so an idle pin never reads as a key.
`analogRead()` must discard its first sample — ADC1 is shared with the battery
gauge and the sample-and-hold is contaminated. Skipping that gives phantom
keypresses.

Controls: UP hit, DOWN stand, CONFIRM double, BACK to bet, LEFT/RIGHT change bet.

## Booting app1: how the chicken-and-egg was solved

To run the game, otadata must select slot 1. To capture a known-good "select
slot 1" record you would have to already be booting slot 1. Instead of guessing
those bytes, `tools/mkotadata.py` **derives the CRC scheme from the device's own
otadata records and refuses to emit anything unless it reproduces them.**

The record, from `esp_flash_partitions.h` in the installed SDK (32 bytes, two
copies one 4 KB sector apart):

```c
typedef struct {
    uint32_t ota_seq;        // increasing
    uint8_t  seq_label[20];  // unused
    uint32_t ota_state;      // 2 = VALID, 0xFFFFFFFF = UNDEFINED
    uint32_t crc;            // CRC32 of the ota_seq field ONLY
} esp_ota_select_entry_t;
```

Two things worth knowing, both of which are easy to get wrong:

- **The CRC is not zlib's.** It is reflected CRC-32, poly `0xEDB88320`,
  **init 0**, final XOR `0xFFFFFFFF`. zlib uses init `0xFFFFFFFF` and does not
  match. Brute-forcing the variant against the device's two stored records proved
  it: `seq=3 -> 0xED4A5011`, `seq=2 -> 0x55F63774`, both reproduced.
- **The slot is the index of the winning copy, not `seq % 2`.** The bootloader
  picks whichever copy has the higher `ota_seq`. Readings were copy0=`3`,
  copy1=`2`, so slot 0 (CrossPoint) was live. Writing `4` to copy1 at
  `0xF000` flipped it.

```bash
./tools/switch.sh reader   # erases otadata -> bootloader falls back to app0
```

Still unbreakable: a wrong or corrupt record is simply ignored, the other copy
wins, and you land in CrossPoint.

## Getting on and off the device is asymmetric

```
CrossPoint -> game :  ./tools/play.sh          (USB cable required)
game -> CrossPoint :  hold POWER ~1.5 s        (no cable)
```

Leaving is something the firmware can do itself: one otadata write. Coming back
needs something able to write otadata, and CrossPoint has no code to select
slot 1. So the cable is not a shortcut I was too lazy to remove - it is a
property of where the code lives. The only ways to remove it are to teach
CrossPoint to flip slots (an upstream change) or to ship the game as the default
boot and accept the reverse asymmetry, which is one record at `0xF000`.

`play.sh` reads the whole 8 KB otadata partition in **one** esptool call and
slices it locally. Two consecutive esptool invocations do not work reliably:
when the ROM bootloader takes over from running firmware the USB CDC port
re-enumerates, and the second call frequently cannot reopen the node.

It also retries the write and exits non-zero on failure. An earlier version
printed "done" after its write had silently failed - the same class of defect as
CrossPoint's docs claiming uploads overwrite existing files.

## Bring-up order

```bash
brew install platformio
./tools/test.sh                  # must be green before touching hardware
pio run -e xteink_x3
./tools/flash.sh                 # writes app1, re-reads and verifies

# In practice just use ./tools/play.sh. The manual sequence, for understanding
# what it does - note the SINGLE 8 KB read; two esptool calls often fail because
# the USB port re-enumerates when the ROM bootloader takes over:
esptool ... read-flash 0xE000 8192 /tmp/ota.bin
python3 - <<'PY'
d = open('/tmp/ota.bin','rb').read()
open('/tmp/c0.bin','wb').write(d[0x000:0x020])      # copy #0
open('/tmp/c1.bin','wb').write(d[0x1000:0x1020])    # copy #1
PY
python3 tools/mkotadata.py --slot 1 --copy0 /tmp/c0.bin --copy1 /tmp/c1.bin \
        --out /tmp/app1-selector.bin
esptool ... write-flash 0xF000 /tmp/app1-selector.bin

# watch it boot (pio device monitor needs a TTY; use the bundled pyserial)
/opt/homebrew/Cellar/platformio/6.2.0/libexec/bin/python3 - <<'PY'
import serial, time
s = serial.Serial("/dev/cu.usbmodem8401", 115200, timeout=1)
time.sleep(.3); s.reset_input_buffer()
s.dtr = False; s.rts = True; time.sleep(.2); s.rts = False   # RTS = reset on C3
end = time.time() + 10
while time.time() < end:
    n = s.in_waiting
    if n: print(s.read(n).decode("utf8", "replace"), end="")
    else: time.sleep(.05)
PY
```

If anything looks wrong: hold POWER 1.5 s, or erase otadata (`switch.sh reader`),
or re-flash CrossPoint to `0x10000` from the backup. All three land on the reader.

## Log output

`pio device monitor` at 115200 (USB-Serial-JTAG). Panels prints
`EPD: BUSY stuck (...)` with the UC8279d hint if the controller guess is wrong,
and `KEY <name> adc1=.. adc2=..` for every key press.

## Layout

```
src/panel.cpp     UC8253 driver: init, LUT load, RAM planes, 3 refresh paths
src/lut_x3.h      generated waveforms (tools/gen_luts.py, asserts 42 bytes)
src/input.cpp     ADC ladder keys, measured buckets, debounce
src/gfx.cpp       1bpp primitives, text, cards, menu rows
src/gfx_font.h    generated proportional atlas: 18/26/40/64 px + 64px suits
                  (tools/font_gen.py, real Helvetica Neue / Arial Bold)
src/blackjack.h   rules: shoe, soft aces, dealer, settlement (pure C++, host-testable)
src/main.cpp      screens: menu, panel test, key test, bet, game
```

`blackjack.h` has no Arduino includes on purpose — that is what makes 800k
host-run assertions possible, and it is how a real bug got caught: rank was
computed as `card / SUITS` without `% RANKS`, so any card from deck 1+ of the
6-deck shoe decoded to rank 13–25. Every total was silently wrong and
`rankName()` read out of bounds. Unit tests that only built hands from deck-0
card ids passed anyway; only the whole-shoe soak caught it. A bug like that is
invisible on an e-ink screen until it has cost you the hand.

## Known gaps

Ordered by what will actually bite you, not by how interesting they are.

1. **No idle sleep, and a static screen looks like a hang.** This already cost
   time: the menu is a still image on e-ink, which is visually identical to a
   freeze, and a long POWER hold was the reasonable response to it. Two separate
   problems - no liveness cue, and no timeout. Leaving a static frame up
   indefinitely also risks image retention on this glass. Fixing this is the
   next change after the screenshot harness.
2. **Key mapping unverified** for the six ADC keys (POWER is confirmed). KEY
   TEST exists to settle it in about twenty seconds.
3. **Type has never been reviewed from a photo.** It was designed blind. Real
   panel capture fed back into the agent is the missing tool.
4. No split pairs, no insurance, no surrender. Betting is fixed chips ±5, no
   discard tray. Dealer stands on all 17 including soft.
5. No high-score persistence - writing to NVS is deliberately not implemented.
6. `fillRect` only has the fast byte-run path for `EPD_ROT=90`. The other three
   orientations fill pixel-by-pixel: correct, slower, and intended for
   diagnostics rather than play.

## Credit

Panel LUTs, init sequence and button ADC values are transcribed from
[PapyriX](https://github.com/bigbag/papyrix-reader) (MIT), which is where
real-device measurements live. Build recipe follows
[microslate-firmware](https://github.com/Josh-writes/microslate-firmware).
Prior art that this is possible on the hardware:
[xteink-tamagotchi](https://github.com/maddiedreese/xteink-tamagotchi).
