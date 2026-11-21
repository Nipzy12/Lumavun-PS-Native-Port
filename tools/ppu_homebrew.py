"""Produce an original PowerPC64 big-endian ELF demo, without PS3 firmware/assets.

This uses a custom demo-only syscall. It is not a PS3 executable with CellOS ABI.
"""
import argparse
from pathlib import Path
import struct

BASE = 0x10000
OFFSET = 0x1000


def addi(rd, ra, imm):
    return (14 << 26) | (rd << 21) | (ra << 16) | (imm & 0xFFFF)


def add(rd, ra, rb):
    return (31 << 26) | (rd << 21) | (ra << 16) | (rb << 11) | (266 << 1)


def cmpwi(ra, imm):
    return (11 << 26) | (ra << 16) | (imm & 0xFFFF)


def bc_ne(from_index, target_index):
    disp = (target_index - from_index) * 4
    if not (-32768 <= disp <= 32764):
        raise ValueError('conditional branch too far')
    return (16 << 26) | (4 << 21) | (2 << 16) | (disp & 0xFFFC)


def bl(from_index, target_index):
    disp = (target_index - from_index) * 4
    return (18 << 26) | (disp & 0x03FFFFFC) | 1


def words():
    # Execute original PPC instructions to calculate 1+2+3+4+5.
    # Index 2 calls subroutine at 5, index 9 returns to 3, index 4 ends via demo sc.
    return [
        addi(3, 0, 0),
        addi(4, 0, 0),
        bl(2, 5),
        addi(3, 4, 0),
        0x44000002,  # custom demo-only syscall; not PS3 CellOS
        addi(3, 3, 1),
        add(4, 4, 3),
        cmpwi(3, 5),
        bc_ne(8, 5),
        0x4E800020,  # blr
    ]


def create_elf(path):
    payload = b''.join(struct.pack('>I', word) for word in words())
    if len(payload) > 4096:
        raise ValueError('fixture too large')
    header = bytearray(64)
    header[:7] = b'\x7fELF\x02\x02\x01'
    struct.pack_into('>HHIQQQIHHHHHH', header, 16,
                     2, 21, 1, BASE, 64, 0, 0, 64, 56, 1, 0, 0, 0)
    ph = struct.pack('>IIQQQQQQ', 1, 5, OFFSET, BASE, BASE,
                     len(payload), len(payload), 0x1000)
    result = bytes(header) + ph + bytes(OFFSET - 64 - len(ph)) + payload
    path = Path(path)
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(result)
    return len(words())


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('output', type=Path)
    args = parser.parse_args()
    print('Generated original PPC64 ELF, words:', create_elf(args.output))
