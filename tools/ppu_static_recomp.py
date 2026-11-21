"""Fail-closed PPC64 ELF -> host C++ static translator for a tiny homebrew ABI.

This is NOT a PS3/CellOS game recompiler. Only explicit whitelisted PowerPC
instructions are allowed, and no console instruction decoder runs at runtime.
"""
import argparse
from pathlib import Path
import struct


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
    binary = Path(path).read_bytes()
    if len(binary) > 1024 * 1024:
        raise RecompError('Prototype ELF input exceeds 1 MiB safety limit')
    if len(binary) < 120 or binary[:7] != b'\x7fELF\x02\x02\x01':
        raise RecompError('Requires plain ELF64 big-endian PowerPC (not SELF)')
    fields = struct.unpack_from('>HHIQQQIHHHHHH', binary, 16)
    elf_type, machine, version, entry, phoff, _, flags, ehsize, phsize, phcount, _, _, _ = fields
    if elf_type != 2 or machine != 21 or version != 1 or ehsize != 64 or phsize != 56 or phcount != 1:
        raise RecompError('Unsupported ELF64 PPU header or multiple program headers')
    if phoff < 64 or phoff > len(binary) - 56:
        raise RecompError('Program header out of bounds')
    ptype, pflags, offset, vaddr, _, filesz, memsz, align = struct.unpack_from('>IIQQQQQQ', binary, phoff)
    if ptype != 1 or pflags & 1 == 0 or not (0 < filesz <= 4096) or filesz != memsz:
        raise RecompError('Expected a tiny executable PT_LOAD section without BSS')
    if offset > len(binary) or filesz > len(binary) - offset or filesz % 4 or vaddr % 4:
        raise RecompError('Invalid executable segment alignment or bounds')
    if not vaddr <= entry < vaddr + filesz or (entry - vaddr) % 4:
        raise RecompError('Entry address is outside executable segment')
    code = binary[offset:offset + filesz]
    return vaddr, (entry - vaddr) // 4, struct.unpack('>' + 'I' * (len(code) // 4), code)


def classify(word, pc, base, count):
    op = word >> 26
    rt = (word >> 21) & 31
    ra = (word >> 16) & 31
    rb = (word >> 11) & 31
    imm = signed16(word & 0xFFFF)
    if op == 14:
        return ('compute', f'r[{rt}] = ' + (f'r[{ra}]' if ra else 'UINT64_C(0)') +
                f' + static_cast<uint64_t>({imm});')
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
    base, entry, words = load_ppu_elf(input_path)
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
        '  uint64_t lr = 0;', '  bool eq = false;', '  uint64_t steps = 0;',
        f'  goto L_{entry};',
    ]
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
            out.append('  return (r[3]==15 && r[6]==15 && r[7]==15 && r[10]==18 && r[11]==52) ? 0 : 7;')
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
