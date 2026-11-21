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


def memory_op(opcode, reg, ra, offset):
    return (opcode << 26) | (reg << 21) | (ra << 16) | (offset & 0xffff)


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


def words(guest_pointer=0x2000):
    if not 0 <= guest_pointer <= 0x7fff:
        raise ValueError('Demo guest pointer must fit positive signed addi immediate')
    # Execute original PPC instructions to calculate 1+2+3+4+5.
    # Index 2 calls the subroutine at 5; the loop at 5-8 then uses mapped guest RAM.
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
        addi(5, 0, guest_pointer),       # demo guest RAM address, not PS3 MMIO
        memory_op(62, 4, 5, 0),  # std r4,0(r5), 64-bit big-endian store
        memory_op(58, 6, 5, 0),  # ld r6,0(r5), 64-bit big-endian load
        memory_op(36, 6, 5, 8),  # stw r6,8(r5), 32-bit big-endian store
        memory_op(32, 7, 5, 8),  # lwz r7,8(r5), zero-extended 32-bit load
        addi(4, 7, 0),           # put loaded value back in result register
        addi(8, 0, 0x1234),
        memory_op(36, 8, 5, 12), # stw r8,12(r5)
        memory_op(34, 10, 5, 14),# lbz => 0x12 (18), confirms big-endian byte
        memory_op(34, 11, 5, 15),# lbz => 0x34 (52)
        0x4E800020,  # blr
    ]


def create_elf(path, guest_pointer=0x2000):
    payload = b''.join(struct.pack('>I', word) for word in words(guest_pointer))
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
    parser.add_argument('--guest-pointer', type=lambda value: int(value, 0), default=0x2000)
    args = parser.parse_args()
    print('Generated original PPC64 ELF, words:', create_elf(args.output, args.guest_pointer))
