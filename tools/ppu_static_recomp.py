"""Fail-closed PPC64 ELF -> host C++ static translator for a tiny homebrew ABI.

This is NOT a PS3/CellOS game recompiler. Only explicit whitelisted PowerPC
instructions are allowed, and no console instruction decoder runs at runtime.
"""
import argparse
from pathlib import Path
import struct

from ppu_elf_layout import ElfLayoutError, parse_ppu_image


class RecompError(ValueError):
    pass


def signed16(n):
    return n - 0x10000 if n & 0x8000 else n


def branch_target(word, pc, conditional):
    if word & 0x2:
        raise RecompError('Absolute PowerPC branches are not supported')
    if conditional:
        displacement = word & 0xFFFC
        if displacement & 0x8000:
            displacement -= 0x10000
    else:
        displacement = word & 0x03FFFFFC
        if displacement & 0x02000000:
            displacement -= 0x04000000
    return pc + displacement


def load_ppu_elf(path):
    try:
        return parse_ppu_image(path)
    except ElfLayoutError as error:
        raise RecompError(str(error)) from error


def classify(word, pc, base, count):
    op = word >> 26
    rt = (word >> 21) & 31
    ra = (word >> 16) & 31
    rb = (word >> 11) & 31
    imm = signed16(word & 0xFFFF)
    if op == 14:
        return ('compute', f'r[{rt}] = ' + (f'r[{ra}]' if ra else 'UINT64_C(0)') +
                f' + static_cast<uint64_t>({imm});')
    if op == 62 and (word & 3) == 1:
        # stdu RS,DS(RA): store original RS value at EA, THEN update RA.
        # The stack-frame backlink is written before the stack pointer changes.
        if ra == 0:
            raise RecompError('stdu requires a nonzero base register')
        disp = signed16(word & 0xfffc)
        address = f'(r[{ra}] + static_cast<uint64_t>({disp}))'
        return ('compute',
                f'if (!guest_store(memory, {address}, 8, r[{rt}])) return 12; '
                f'r[{ra}] = {address};')
    if op in (32, 34, 36, 58, 62):
        if op in (58, 62) and (word & 3) != 0:
            raise RecompError('Unsupported PPC64 DS-form variant (update or lwa)')
        displacement = signed16((word & 0xfffc) if op in (58, 62) else (word & 0xffff))
        base_expr = f'r[{ra}]' if ra else 'UINT64_C(0)'
        address = f'({base_expr} + static_cast<uint64_t>({displacement}))'
        width = {32: 4, 34: 1, 36: 4, 58: 8, 62: 8}[op]
        if op in (32, 34, 58):
            return ('compute', f'if (!guest_load(memory, {address}, {width}, r[{rt}])) return 12;')
        return ('compute', f'if (!guest_store(memory, {address}, {width}, r[{rt}])) return 12;')
    if op == 31 and (word & 1) == 0 and ((word >> 1) & 1023) in (339, 467):
        # PowerPC XFX SPR encoding swaps the upper/lower 5-bit halves.
        encoded_spr = (word >> 11) & 1023
        spr = ((encoded_spr & 31) << 5) | (encoded_spr >> 5)
        if spr != 8:
            raise RecompError('Only LR (SPR 8) moves are supported')
        xo = (word >> 1) & 1023
        return ('compute', f'r[{rt}] = lr;' if xo == 339 else f'lr = r[{rt}];')
    if op == 31 and ((word >> 1) & 1023) == 266 and word & 1 == 0:
        return ('compute', f'r[{rt}] = r[{ra}] + r[{rb}];')
    if op == 11 and rt == 0:
        return ('compute', f'eq = (static_cast<int32_t>(static_cast<uint32_t>(r[{ra}])) == {imm});')
    if op == 16 and rt == 4 and ra == 2 and word & 1 == 0:
        target = branch_target(word, pc, True)
        if not base <= target < base + 4 * count or target % 4:
            raise RecompError('Conditional branch target outside executable segment')
        return ('conditional', (target - base) // 4)
    if op == 18:
        target = branch_target(word, pc, False)
        if not base <= target < base + 4 * count or target % 4:
            raise RecompError('Direct branch target outside executable segment')
        return ('call' if word & 1 else 'branch', (target - base) // 4)
    if word == 0x4E800020:
        return ('return', None)
    if word == 0x44000002:
        return ('hostcall', None)
    raise RecompError(f'Unsupported PPC64 word 0x{word:08X} at PC 0x{pc:X}')


GUEST_MEMORY_HELPERS = r"""
// Original PPU homebrew has a sandboxed *demo* guest RAM region.
// This is NOT PS3 CellOS virtual memory or actual PS3 MMIO.
using GuestRam = std::array<uint8_t, 65536>;
static constexpr uint64_t guest_base = UINT64_C(0x2000);
static bool guest_address_valid(uint64_t address, size_t width) {
    if (width != 1 && width != 4 && width != 8) return false;
    if ((address % width) != 0 || address < guest_base) return false;
    const uint64_t offset = address - guest_base;
    return offset <= 65536 - width;
}
static bool guest_load(const GuestRam& memory, uint64_t address,
                       size_t width, uint64_t& value) {
    if (!guest_address_valid(address, width)) return false;
    const size_t offset = static_cast<size_t>(address - guest_base);
    uint64_t result = 0;
    for (size_t i=0; i<width; ++i)
        result = (result << 8) | memory[offset+i]; // PPC big-endian loads
    value = result;
    return true;
}
static bool guest_store(GuestRam& memory, uint64_t address,
                        size_t width, uint64_t value) {
    if (!guest_address_valid(address, width)) return false;
    const size_t offset = static_cast<size_t>(address - guest_base);
    for (size_t i=0; i<width; ++i) {
        memory[offset+width-1-i] = static_cast<uint8_t>(value);
        value >>= 8;
    }
    return true;
}
"""


def generate(input_path):
    image = load_ppu_elf(input_path)
    base, entry, words = image.code_base, image.entry_index, image.words
    decoded = []
    returns = []
    for index, word in enumerate(words):
        kind, arg = classify(word, base + index * 4, base, len(words))
        decoded.append((kind, arg))
        if kind == 'call':
            returns.append((base + (index + 1) * 4, index + 1))
    if not any(kind == 'hostcall' for kind, _ in decoded):
        raise RecompError('Demo hostcall is required; no output can be produced')
    if not any(kind == 'call' for kind, _ in decoded):
        raise RecompError('Demonstration requires at least one PowerPC branch-and-link')
    out = [
        '// Generated from an original PPC64 ELF. Not PS3 CellOS compatible.',
        '#include <cstdint>', '#include <iostream>', '#include <array>',
        GUEST_MEMORY_HELPERS,
        'int main() {', '  GuestRam memory{};', '  uint64_t r[32] = {};',
        '  r[1] = UINT64_C(0x7000); // demo-specific initial PPC stack pointer',
        '  uint64_t lr = 0;', '  bool eq = false;', '  uint64_t steps = 0;',
    ]
    # All ELF data segments are validated as mapped to the fictional sandbox.
    # Filesz bytes are initialized; the remaining memsz-filesz bytes stay zero.
    for data_segment in image.data_segments:
        offset = data_segment.virtual_address - 0x2000
        for i, byte in enumerate(data_segment.contents):
            out.append(f'  memory[{offset+i}] = static_cast<uint8_t>({byte});')
    # ET_EXEC with B=0 only: R_PPC64_RELATIVE = B + addend.
    # Apply the relocation BEFORE jumping into translated PowerPC instructions.
    for relocation in image.relocations:
        out.append(
            f'  if (!guest_store(memory, UINT64_C({relocation.virtual_address}), '
            f'8, UINT64_C({relocation.value}))) return 12;')
    out.append(f'  goto L_{entry};')
    for index, (kind, arg) in enumerate(decoded):
        out.append(f'L_{index}: {{')
        out.append('  if (++steps > UINT64_C(1000000)) return 10;')
        continuation = f'goto L_{index+1};' if index + 1 < len(decoded) else 'return 9;'
        if kind == 'compute':
            out.append(f'  {arg}')
            out.append(f'  {continuation}')
        elif kind == 'conditional':
            out.append(f'  if (!eq) goto L_{arg};')
            out.append(f'  {continuation}')
        elif kind == 'branch':
            out.append(f'  goto L_{arg};')
        elif kind == 'call':
            out.append(f'  lr = UINT64_C({base + (index+1)*4});')
            out.append(f'  goto L_{arg};')
        elif kind == 'return':
            for address, after_call in returns:
                out.append(f'  if (lr == UINT64_C({address})) goto L_{after_call};')
            out.append('  return 8; // unrecognized link register')
        elif kind == 'hostcall':
            out.append('  std::cout << "PPU_HOMEBREW_RESULT=" << r[3] << "\\n";')
            out.append('  std::cout << "PPU_GUEST_BE_BYTES=" << r[10] << "," << r[11] << "\\n";')
            out.append(f'  std::cout << "PPU_STACK_RESTORED=" << (r[1] == UINT64_C(0x7000) && lr == UINT64_C({base + 12})) << "\\n";')
            out.append('  std::cout << "PPU_ELF_RELOCATED=" << (r[16] == UINT64_C(0x2000) && r[17] == UINT64_C(15) && r[18] == UINT64_C(171) && r[19] == UINT64_C(0)) << "\\n";')
            out.append(f'  return (r[3]==15 && r[6]==15 && r[7]==15 && r[10]==18 && r[11]==52 && r[1] == UINT64_C(0x7000) && lr == UINT64_C({base + 12}) && r[16] == UINT64_C(0x2000) && r[17] == UINT64_C(15) && r[18] == UINT64_C(171) && r[19] == UINT64_C(0)) ? 0 : 7;')
        out.append('}')
    out.append('}')
    return '\n'.join(out) + '\n'


def translate(input_path, output_path):
    # Always validate and generate in memory first. Never write partial success.
    cpp = generate(input_path)
    output_path = Path(output_path)
    if Path(input_path).resolve() == output_path.resolve():
        raise RecompError('Cannot overwrite the source game executable')
    output_path.parent.mkdir(parents=True, exist_ok=True)
    output_path.write_text(cpp, encoding='utf-8')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description='Demo-only static PPC64 ELF -> native C++ subset translator')
    parser.add_argument('input', type=Path)
    parser.add_argument('output', type=Path)
    args = parser.parse_args()
    try:
        translate(args.input, args.output)
    except (OSError, RecompError) as exc:
        parser.error(str(exc))
    print('PPC64 demo statically translated:', args.output)
