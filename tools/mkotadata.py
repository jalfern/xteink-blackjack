#!/usr/bin/env python3
"""Generate a valid ESP-IDF otadata selector for a chosen app slot.

Why this exists: booting app1 the first time needs an otadata record, and an
otadata record carries a CRC. Rather than hand-synthesise bytes and hope, this
tool DERIVES the CRC scheme from the device's own records and refuses to emit
anything unless it reproduces them exactly.

Layout, from the installed SDK
(esp_flash_partitions.h, esp_ota_select_entry_t - 32 bytes, two copies one
flash sector apart at otadata+0x0000 and otadata+0x1000):

    uint32_t ota_seq;        // monotonically increasing
    uint8_t  seq_label[20];  // unused here
    uint32_t ota_state;      // 2 = IMG_VALID, 0xFFFFFFFF = UNDEFINED
    uint32_t crc;            // CRC32 of the ota_seq field only

The bootloader picks the copy with the HIGHER ota_seq, and the slot is the
INDEX of that copy - not the parity of the sequence number. So to boot slot 1
you write copy #1 with a seq greater than copy #0's.

CRC, established by brute-forcing against the two records read off the device:
reflected CRC-32, poly 0xEDB88320, init 0, final XOR 0xFFFFFFFF. (Not zlib's,
which uses init 0xFFFFFFFF - that one does NOT match.)

If a generated record were ever wrong the device does NOT brick: an invalid
copy is ignored, the other copy wins, and you land back in CrossPoint.
"""
import argparse, os, struct, sys

OTADATA_BASE = 0xE000
SECTOR = 0x1000
IMG_VALID = 0x00000002
IMG_UNDEFINED = 0xFFFFFFFF


def crc32_ota(buf: bytes) -> int:
    """ESP bootloader otadata CRC: reflected CRC-32, init 0, xorout all-ones."""
    crc = 0
    for b in buf:
        crc ^= b
        for _ in range(8):
            crc = (crc >> 1) ^ 0xEDB88320 if crc & 1 else crc >> 1
    return crc ^ 0xFFFFFFFF


def parse(rec: bytes, where: str):
    if len(rec) < 32:
        sys.exit(f"{where}: only {len(rec)} bytes")
    seq = int.from_bytes(rec[0:4], "little")
    state = int.from_bytes(rec[24:28], "little")
    crc = int.from_bytes(rec[28:32], "little")
    mine = crc32_ota(rec[0:4])
    return seq, state, crc, mine, (mine == crc)


def build(seq: int, state: int) -> bytes:
    body = seq.to_bytes(4, "little") + b"\xff" * 20 + state.to_bytes(4, "little")
    return body + crc32_ota(body[0:4]).to_bytes(4, "little")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--slot", type=int, choices=(0, 1), required=True)
    ap.add_argument("--copy0", required=True, help="32 bytes read from otadata+0x0000")
    ap.add_argument("--copy1", required=True, help="32 bytes read from otadata+0x1000")
    ap.add_argument("--out", required=True)
    a = ap.parse_args()

    d0 = open(a.copy0, "rb").read()
    d1 = open(a.copy1, "rb").read()

    print("Deriving CRC scheme from the device's own records:")
    ok = True
    for tag, d in (("copy#0", d0), ("copy#1", d1)):
        seq, state, crc, mine, good = parse(d, tag)
        print(f"  {tag}: seq={seq:<3} state=0x{state:08X} stored=0x{crc:08X} "
              f"computed=0x{mine:08X}  {'MATCH' if good else '*** NO MATCH ***'}")
        ok &= good
    if not ok:
        sys.exit("\nRefusing to generate: CRC scheme does not reproduce the "
                 "device's records. Do not write anything.")

    seq0 = int.from_bytes(d0[0:4], "little")
    seq1 = int.from_bytes(d1[0:4], "little")
    active = 0 if seq0 >= seq1 else 1
    print(f"\n  active slot now: {active}  (seq {seq0} vs {seq1})")

    new_seq = max(seq0, seq1) + 1
    rec = build(new_seq, IMG_UNDEFINED)
    seq, state, crc, mine, good = parse(rec, "generated")
    assert good, "self-check failed"

    dest = os.path.abspath(a.out)
    open(dest, "wb").write(rec)

    addr = OTADATA_BASE + a.slot * SECTOR
    print(f"\n  wrote {dest}")
    print(f"    seq={new_seq} state=UNDEFINED crc=0x{crc:08X} (self-verified)")
    print(f"\n  FLASH WITH (this is the only command that should use this file):")
    print(f"    esptool ... write-flash 0x{addr:X} {dest}")
    print(f"  0x{addr:X} is inside otadata "
          f"(0x{OTADATA_BASE:X}..0x{OTADATA_BASE + 2 * SECTOR - 1:X}) - "
          f"{'OK' if OTADATA_BASE <= addr < OTADATA_BASE + 2 * SECTOR else 'OUT OF RANGE'}")
    if not (OTADATA_BASE <= addr < OTADATA_BASE + 2 * SECTOR):
        sys.exit("refusing to emit an address outside otadata")
    print(f"\n  target slot: app{a.slot}   (erase otadata to return to CrossPoint)")


if __name__ == "__main__":
    main()
