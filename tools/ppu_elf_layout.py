"""Strict ELF64 big-endian PowerPC demo-layout parser.

Allows 1 RX PT_LOAD plus up to 3 RW data PT_LOAD regions. The code stays a
single <=4 KiB segment; data is mapped only into fictional 64 KiB guest RAM.
Recognizes *only* R_PPC64_RELATIVE (22) RELA records with sym=0, B=0 in this
fixed ET_EXEC demo. Never handles SELF, dynamic linking or real PS3 memory.
"""
from dataclasses import dataclass
from pathlib import Path
import struct

GUEST_BASE = 0x2000
GUEST_END = GUEST_BASE + 65536
MAX_FILE = 1024 * 1024
R_PPC64_RELATIVE = 22


class ElfLayoutError(ValueError):
    pass


@dataclass(frozen=True)
class DataSegment:
    virtual_address: int
    contents: bytes
    memory_size: int


@dataclass(frozen=True)
class RelativeRelocation:
    virtual_address: int
    value: int


@dataclass(frozen=True)
class PpuImage:
    code_base: int
    entry_index: int
    words: tuple
    data_segments: tuple
    relocations: tuple


def fail_if_outside(blob, start, length, label):
    if start < 0 or length < 0 or start > len(blob) or length > len(blob)-start:
        raise ElfLayoutError(f'{label} outside ELF file')


def overlaps(a, a_len, b, b_len):
    return a < b + b_len and b < a + a_len


def parse_ppu_image(path):
    blob = Path(path).read_bytes()
    if len(blob) > MAX_FILE:
        raise ElfLayoutError('Prototype ELF input exceeds 1 MiB')
    if len(blob) < 64 or blob[:7] != b'\x7fELF\x02\x02\x01':
        raise ElfLayoutError('Requires plain ELF64 big-endian PowerPC, not a SELF')
    (kind, machine, version, entry, phoff, shoff, flags, ehsize,
     phsize, phnum, shsize, shnum, shstr) = struct.unpack_from(
        '>HHIQQQIHHHHHH', blob, 16)
    if (kind != 2 or machine != 21 or version != 1 or flags != 0 or
        ehsize != 64 or phsize != 56 or not (1 <= phnum <= 4)):
        raise ElfLayoutError('Unsupported ELF header or program header count')
    if shstr != 0:
        raise ElfLayoutError('Section-name index not supported in this minimal demo')
    fail_if_outside(blob, phoff, phnum * 56, 'Program header table')
    if shnum:
        if not (1 <= shnum <= 16) or shsize != 64:
            raise ElfLayoutError('Unsupported section header count/size')
        fail_if_outside(blob, shoff, shnum * 64, 'Section header table')
    elif shoff != 0:
        raise ElfLayoutError('Section header offset without table')

    segments = []
    code = None
    data = []
    for i in range(phnum):
        ptype, perms, offset, va, pa, filesz, memsz, align = struct.unpack_from(
            '>IIQQQQQQ', blob, phoff + i * phsize)
        if ptype != 1:
            raise ElfLayoutError('Only PT_LOAD is implemented for this demo')
        if (align not in (0, 1) and
            (align & (align-1) or offset % align != va % align)):
            raise ElfLayoutError('Invalid segment alignment/congruence')
        if memsz == 0 or filesz > memsz:
            raise ElfLayoutError('Invalid segment memory size')
        fail_if_outside(blob, offset, filesz, 'PT_LOAD contents')
        if va > (1 << 64) - 1 - memsz:
            raise ElfLayoutError('Virtual address overflow')
        if any(overlaps(va, memsz, addr, size) for addr, size in segments):
            raise ElfLayoutError('Overlapping ELF memory segments')
        segments.append((va, memsz))
        if perms == 5:
            if code is not None or filesz != memsz or filesz == 0 or filesz > 4096 or (
                filesz % 4 or va % 4):
                raise ElfLayoutError('Requires exactly one bounded RX code PT_LOAD')
            code = (va, blob[offset:offset+filesz])
        elif perms == 6:
            if not (GUEST_BASE <= va < GUEST_END and
                    memsz <= 4096 and memsz <= GUEST_END-va and
                    va % 8 == 0):
                raise ElfLayoutError('RW segment outside bounded demo guest RAM')
            data.append(DataSegment(va, blob[offset:offset+filesz], memsz))
        else:
            raise ElfLayoutError('Only RX code and RW data segments are supported')
    if code is None:
        raise ElfLayoutError('Executable PT_LOAD segment required')
    va, code_bytes = code
    if not (va <= entry < va + len(code_bytes) and (entry-va) % 4 == 0):
        raise ElfLayoutError('Entry address must be in executable segment')
    if len(data) > 3:
        raise ElfLayoutError('Too many data segments')

    relocations = []
    seen_targets = set()
    if shnum:
        for idx in range(shnum):
            (name, shtype, shflags, addr, offset, size, link, info, addralign, entsize) = struct.unpack_from(
                '>IIQQQQIIQQ', blob, shoff + idx * shsize)
            if shtype == 9:
                raise ElfLayoutError('SHT_REL implicit-addend relocations not supported')
            if shtype != 4:  # Only SHT_RELA processed, other sections are metadata
                continue
            if (size > 32 * 24 or size % 24 or entsize != 24 or
                addralign not in (0, 1, 8) or link != 0 or info != 0):
                raise ElfLayoutError('Unsupported RELA section layout')
            fail_if_outside(blob, offset, size, 'RELA records')
            for at in range(offset, offset+size, 24):
                target, rinfo, addend = struct.unpack_from('>QQq', blob, at)
                symbol = rinfo >> 32
                rtype = rinfo & 0xffffffff
                if rtype != R_PPC64_RELATIVE or symbol != 0:
                    raise ElfLayoutError('Only symbol-free R_PPC64_RELATIVE is supported')
                if target % 8 or target in seen_targets:
                    raise ElfLayoutError('Unaligned or duplicate relocation target')
                if not any(s.virtual_address <= target and
                           target+8 <= s.virtual_address+s.memory_size
                           for s in data):
                    raise ElfLayoutError('RELA target outside writable data PT_LOAD')
                # ET_EXEC fixed-bias prototype: B=0, result=B+A.
                if not (GUEST_BASE <= addend < GUEST_END):
                    raise ElfLayoutError('RELA addend outside guest RAM')
                seen_targets.add(target)
                relocations.append(RelativeRelocation(target, addend))
    code_words = struct.unpack('>'+'I'*(len(code_bytes)//4), code_bytes)
    return PpuImage(va, (entry-va)//4, code_words,
                    tuple(data), tuple(relocations))
