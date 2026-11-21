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


def addis(rd, ra, imm):
    # PowerPC ADDIS (also LIS when RA=0). Signed immediate shifted by 16.
    return (15 << 26) | (rd << 21) | (ra << 16) | (imm & 0xffff)


def ori(rd, ra, imm):
    return (24 << 26) | (rd << 21) | (ra << 16) | (imm & 0xffff)


def xori(rd, ra, imm):
    return (26 << 26) | (rd << 21) | (ra << 16) | (imm & 0xffff)


def add(rd, ra, rb):
    return (31 << 26) | (rd << 21) | (ra << 16) | (rb << 11) | (266 << 1)


def memory_op(opcode, reg, ra, offset):
    return (opcode << 26) | (reg << 21) | (ra << 16) | (offset & 0xffff)


def mflr(reg):
    # mfspr reg,LR (SPR 8)
    return (31 << 26) | (reg << 21) | (8 << 16) | (339 << 1)


def mtlr(reg):
    # mtspr LR,reg
    return (31 << 26) | (reg << 21) | (8 << 16) | (467 << 1)


def stdu(reg, ra, offset):
    if ra == 0 or offset % 4:
        raise ValueError('stdu requires nonzero RA and 4-byte DS displacement')
    return (62 << 26) | (reg << 21) | (ra << 16) | (offset & 0xfffc) | 1


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
    # Main calls worker at 5. Worker saves LR + stack frame before calling
    # nested memory helper at 17; both frames are restored before hostcall.
    return [
        addi(3, 0, 0),        # 0: result
        addi(4, 0, 0),        # 1: accumulator
        bl(2, 5),            # 2: main -> worker
        addi(3, 4, 0),       # 3: copy result
        0x44000002,          # 4: custom homebrew hostcall, NOT CellOS
        mflr(12),            # 5: worker saves return from main
        stdu(1, 1, -32),     # 6: allocate PPC64 stack frame
        memory_op(62, 12, 1, 16), # 7: std saved LR
        addi(3, 3, 1),       # 8: loop
        add(4, 4, 3),        # 9
        cmpwi(3, 5),         # 10
        bc_ne(11, 8),        # 11
        bl(12, 17),          # 12: nested function call replaces LR
        memory_op(58, 12, 1, 16), # 13: ld previous return address
        mtlr(12),            # 14: restore LR before worker return
        memory_op(58, 1, 1, 0),  # 15: restore SP using stack backlink
        0x4E800020,          # 16: blr to main
        stdu(1, 1, -32),     # 17: nested helper creates its own frame
        addi(5, 0, guest_pointer),  # 18: data region; not PS3 MMIO
        memory_op(62, 4, 5, 0),   # 19: std value
        memory_op(58, 6, 5, 0),   # 20: ld value
        memory_op(36, 6, 5, 8),   # 21: stw value
        memory_op(32, 7, 5, 8),   # 22: lwz value
        addi(4, 7, 0),            # 23: restored result
        addi(8, 0, 0x1234),       # 24
        memory_op(36, 8, 5, 12),  # 25
        memory_op(34, 10, 5, 14), # 26: big-endian byte 0x12
        memory_op(34, 11, 5, 15), # 27: big-endian byte 0x34
        memory_op(58, 1, 1, 0),   # 28: release nested frame
        addi(15, 0, 0x3000),       # 29: separate ELF RW PT_LOAD segment
        memory_op(58, 16, 15, 0), # 30: ld R_PPC64_RELATIVE pointer (0x2000)
        memory_op(62, 4, 16, 0),  # 31: std result through relocated pointer
        memory_op(58, 17, 16, 0), # 32: ld back via relocated pointer
        memory_op(34, 18, 15, 8), # 33: initialized .data byte 0xab
        memory_op(34, 19, 15, 32),# 34: zero-filled BSS byte
        addis(20, 0, 0x1234),    # 35: 0x12340000 (LIS)
        ori(20, 20, 0x5678),     # 36: 0x12345678
        xori(21, 20, 0x5678),    # 37: 0x12340000
        memory_op(36, 20, 16, 24), # 38: stw immediate result into mapped scratch
        memory_op(32, 22, 16, 24), # 39: lwz round-trip from scratch
        0x4E800020,               # 40: blr to worker
    ]


def create_elf(path, guest_pointer=0x2000):
    payload = b''.join(struct.pack('>I', word) for word in words(guest_pointer))
    if len(payload) > 4096:
        raise ValueError('fixture too large')

    # Two PT_LOADs: separate executable code and RW data (including zero BSS).
    # One SHT_RELA carries a symbol-free R_PPC64_RELATIVE (type 22).
    # Fixed ET_EXEC load bias B=0, relocation value is B+A = 0x2000.
    code_offset = OFFSET
    data_offset = 0x2000
    rela_offset = 0x2020
    section_offset = 0x2040
    total = section_offset + 2 * 64
    image = bytearray(total)
    image[:7] = b'\x7fELF\x02\x02\x01'
    struct.pack_into('>HHIQQQIHHHHHH', image, 16,
                     2, 21, 1, BASE, 64, section_offset, 0,
                     64, 56, 2, 64, 2, 0)
    # RX code segment: at offset 0x1000, virtual address 0x10000
    struct.pack_into('>IIQQQQQQ', image, 64,
                     1, 5, code_offset, BASE, BASE,
                     len(payload), len(payload), 0x1000)
    # RW data with 16 bytes initialized and 64 bytes in memory (BSS).
    struct.pack_into('>IIQQQQQQ', image, 120,
                     1, 6, data_offset, 0x3000, 0x3000, 16, 64, 0x1000)
    image[code_offset:code_offset+len(payload)] = payload
    image[data_offset+8] = 0xab  # initialized .data signature
    # ELF64_R_INFO: sym=0, type=R_PPC64_RELATIVE(22)
    struct.pack_into('>QQq', image, rela_offset, 0x3000, 22, 0x2000)
    # Section #0 is SHT_NULL. Section #1 is SHT_RELA with 24-byte records.
    struct.pack_into('>IIQQQQIIQQ', image, section_offset+64,
                     0, 4, 0, 0, rela_offset, 24, 0, 0, 8, 24)
    path = Path(path)
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(image)
    return len(words())


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('output', type=Path)
    parser.add_argument('--guest-pointer', type=lambda value: int(value, 0), default=0x2000)
    args = parser.parse_args()
    print('Generated original PPC64 ELF, words:', create_elf(args.output, args.guest_pointer))
