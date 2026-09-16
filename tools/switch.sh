#!/usr/bin/env bash
# Choose which firmware boots.
#
#   ./tools/switch.sh reader   -> app0 (CrossPoint)   SAFE, works today
#   ./tools/switch.sh game     -> app1 (this game)    needs a blob, see below
#   ./tools/switch.sh capture  -> save the current otadata bytes
#   ./tools/switch.sh status   -> show what is selected now
#
# HOW THE READER SIDE WORKS
# erasing otadata makes the ESP-IDF bootloader fall back to ota_0, which is
# CrossPoint. So "reader" cannot fail into an unbootable state: a blank,
# corrupt, or wrong otadata all resolve to the reader.
#
# WHY THE GAME SIDE NEEDS A BLOB
# selecting ota_1 means writing a valid otadata record, and an otadata record is
# a struct plus a CRC. I did not want to hand-synthesise those bytes and call it
# done without ever seeing the device, so this script needs a blob captured from
# a boot that actually selected app1. Capture it once (README, step 3) and every
# switch after that is instant. If it has never been captured the script says so
# instead of writing bytes it cannot vouch for.
set -uo pipefail
cd "$(dirname "$0")/.."

ESPTOOL="${ESPTOOL:-$HOME/XteinkX3-backup/tools/esptool-env/bin/esptool}"
PORT="${PORT:-/dev/cu.usbmodem8401}"
DIR="tools/otadata"
OTA=0xE000
LEN=32

[ -x "$ESPTOOL" ] || { echo "esptool not at $ESPTOOL"; exit 1; }
[ -e "$PORT" ] || { echo "no device on $PORT (ls /dev/cu.usb*)"; exit 1; }
mkdir -p "$DIR"

E=( "$ESPTOOL" --chip esp32c3 --port "$PORT" --before default-reset )

case "${1:-status}" in
  status)
    echo "reading otadata..."
    "${E[@]}" --after no-reset read-flash $OTA $LEN "$DIR/current.bin" 2>/dev/null \
      && { echo "  hexdump:"; xxd "$DIR/current.bin" | sed 's/^/    /';
           echo "  (first byte of the record's ota_seq field tells you the slot;"
           echo "   compare against app0.bin / app1.bin once captured)"; } \
      || echo "  could not read - is the device in the bootloader?"
    ;;

  capture)
    "${E[@]}" --after no-reset read-flash $OTA $LEN "$DIR/current.bin"
    cp "$DIR/current.bin" "$DIR/app0.bin"
    shasum -a 256 "$DIR/app0.bin"
    echo "saved tools/otadata/app0.bin (this is the app0/CrossPoint selector)"
    ;;

  reader)
    echo "erasing otadata -> bootloader falls back to ota_0 (CrossPoint)"
    "${E[@]}" --after hard-reset erase-region $OTA $LEN \
      && echo "done. CrossPoint will boot."
    ;;

  game)
    # Delegated on purpose. The selector has to be derived from the device's
    # CURRENT otadata (seq = highest live seq + 1). Rewriting a previously
    # captured record is a trap: the bootloader picks the HIGHER seq copy, so a
    # stale seq=4 loses to a live seq=6, the device quietly stays on CrossPoint,
    # and this script would still have printed "game should now boot".
    # play.sh reads live state, derives the seq, and exits non-zero on failure.
    echo "'game' derives the selector from live flash state - delegating to play.sh"
    exec ./tools/play.sh
    ;;

  *) echo "usage: $0 status|capture|reader|game"; exit 1 ;;
esac
