#!/usr/bin/env bash
# Build and write the game to app1 ONLY.
#
# This script deliberately cannot touch the bootloader, the partition table, or
# nvs. Those three are the only things on this device whose failure looks like a
# brick, and none of them need to change to run a game.
#
# After this finishes the device STILL BOOTS CROSSPOINT, because otadata is
# untouched. Use tools/switch.sh game to actually boot into the game.
set -uo pipefail
cd "$(dirname "$0")/.."

ESPTOL_DEFAULT="$HOME/XteinkX3-backup/tools/esptool-env/bin/esptool"
ESPTOOL="${ESPTOOL:-$ESPTOL_DEFAULT}"
PORT="${PORT:-/dev/cu.usbmodem8401}"
APP1=0x780000
BIN="build_firmware/app1.bin"
YES="${1:-}"

say(){ printf '\n\033[1m%s\033[0m\n' "$*"; }

[ -x "$ESPTOOL" ] || { echo "esptool not at $ESPTOOL (set ESPTOOL=)"; exit 1; }
[ -e "$PORT" ] || { echo "no device on $PORT. List: ls /dev/cu.usb*"; exit 1; }

BACKUP="$HOME/XteinkX3-backup/stock-full-16mb.bin"
if [ ! -f "$BACKUP" ]; then
  echo "REFUSING: full stock backup $BACKUP is missing."
  echo "Do not flash without a verified dump. That rule paid for itself already."
  exit 1
fi

say "1/4 host checks (LUTs, font, rules engine)"
if ! ./tools/test.sh; then echo "host checks failed - not flashing"; exit 1; fi

say "2/4 build"
command -v pio >/dev/null 2>&1 || {
  echo "PlatformIO missing. Install:  brew install platformio   (or: pipx install platformio)"
  exit 1
}
mkdir -p build_firmware
if pio run -e xteink_x3; then
  SRC=".pio/build/xteink_x3/firmware.bin"
  [ -f "$SRC" ] || { echo "build produced no $SRC"; exit 1; }
  cp "$SRC" "$BIN"
else
  echo "BUILD FAILED"; exit 1
fi

SZ=$(stat -f%z "$BIN")
CAP=7798784
echo "firmware $BIN = $SZ bytes (app slot capacity $CAP)"
[ "$SZ" -le "$CAP" ] || { echo "too big for the app slot"; exit 1; }
shasum -a 256 "$BIN"

cat <<EOF

  WILL WRITE : app1  $APP1   $(basename "$BIN")  ($SZ bytes)
  WON'T TOUCH: bootloader 0x0, partition table 0x8000,
               otadata 0xE000, nvs 0x9000, app0 (CrossPoint)

  Device continues to boot CrossPoint after this.
EOF

if [ "$YES" != "--yes" ]; then
  read -r -p "
Type FLASH to continue: " A
  [ "$A" = "FLASH" ] || { echo "aborted"; exit 1; }
fi

say "3/4 write"
"$ESPTOOL" --chip esp32c3 --port "$PORT" --before default-reset --after hard-reset \
  write-flash "$APP1" "$BIN" || { echo "WRITE FAILED"; exit 1; }

say "4/4 verify (re-reads the flash and re-hashes)"
"$ESPTOOL" --chip esp32c3 --port "$PORT" --before default-reset --after hard-reset \
  verify-flash "$APP1" "$BIN" && echo "VERIFIED: app1 matches the built image"

cat <<EOF

Done. Reader is unchanged and still the default boot.
  boot the game :  ./tools/play.sh        (needs USB - see its header comment)
  back to reader:  hold POWER ~1.5s in the game, or ./tools/switch.sh reader
EOF
