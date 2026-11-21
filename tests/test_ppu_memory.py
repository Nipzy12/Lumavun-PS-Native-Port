"""Black-box integration tests for PPC64 guest-memory operations.

Synthetic, owned ELF only. No PS3 firmware or commercial game.
"""
import pathlib
import struct
import subprocess
import sys
import tempfile
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
from ppu_homebrew import create_elf
from ppu_static_recomp import generate, translate, RecompError


def op_d(opcode, reg, ra, signed_offset):
    return (opcode << 26) | (reg << 21) | (ra << 16) | (signed_offset & 0xffff)


def guest_program(path, guest_pointer=0x2000, ds_suffix=0):
    """Replace only the original final BLR with real PPC64 memory instructions."""
    create_elf(path)
    binary = bytearray(path.read_bytes())
    program = [
        op_d(14, 5, 0, guest_pointer),     # addi r5, r0, mapped guest address
        op_d(62, 4, 5, 0) | ds_suffix,    # std r4,0(r5), DS-form
        op_d(58, 6, 5, 0),               # ld r6,0(r5), DS-form
        op_d(36, 6, 5, 8),               # stw r6,8(r5)
        op_d(32, 7, 5, 8),               # lwz r7,8(r5)
        op_d(14, 4, 7, 0),               # addi r4,r7,0; return value
        op_d(14, 8, 0, 0x1234),          # addi r8,0,0x1234
        op_d(36, 8, 5, 12),              # stw r8,12(r5), big-endian
        op_d(34, 10, 5, 14),             # lbz r10,14(r5) -> 0x12
        op_d(34, 11, 5, 15),             # lbz r11,15(r5) -> 0x34
        0x4e800020,                      # blr
    ]
    # In the original ELF the final BLR is at instruction index 9.
    new_payload = bytes(binary[0x1000:0x1000 + 9*4]) + b''.join(
        struct.pack('>I', value) for value in program
    )
    binary = binary[:0x1000] + new_payload
    # Program header: p_filesz and p_memsz, big-endian ELF64 at 64+32/40.
    struct.pack_into('>Q', binary, 96, len(new_payload))
    struct.pack_into('>Q', binary, 104, len(new_payload))
    path.write_bytes(binary)


def run_native(cpp, directory):
    exe = directory / 'native'
    subprocess.run(['g++', '-std=c++17', '-O2', str(cpp), '-o', str(exe)], check=True)
    return subprocess.run([str(exe)], text=True, capture_output=True, timeout=12)


class PPUGuestMemoryTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.directory = pathlib.Path(self.tmp.name)
        self.elf = self.directory / 'MEMORY.ELF'

    def test_real_load_store_compiles_and_preserves_result(self):
        guest_program(self.elf)
        cpp = self.directory / 'native.cpp'
        translate(self.elf, cpp)
        text = cpp.read_text()
        self.assertIn('guest_load(', text)
        self.assertIn('guest_store(', text)
        result = run_native(cpp, self.directory)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn('PPU_HOMEBREW_RESULT=15', result.stdout)
        self.assertIn('PPU_GUEST_BE_BYTES=18,52', result.stdout)

    def test_guest_address_outside_mapping_fails_without_crashing(self):
        guest_program(self.elf, guest_pointer=0x1000)
        cpp = self.directory / 'native.cpp'
        translate(self.elf, cpp)
        result = run_native(cpp, self.directory)
        self.assertEqual(result.returncode, 12)
        self.assertNotIn('PPU_HOMEBREW_RESULT=15', result.stdout)

    def test_guest_address_unaligned_fails_without_crashing(self):
        guest_program(self.elf, guest_pointer=0x2002)
        cpp = self.directory / 'native.cpp'
        translate(self.elf, cpp)
        result = run_native(cpp, self.directory)
        self.assertEqual(result.returncode, 12)

    def test_reject_unsupported_ds_update_form(self):
        guest_program(self.elf, ds_suffix=1)
        with self.assertRaises(RecompError):
            generate(self.elf)

    def test_invalid_elf_never_creates_output(self):
        guest_program(self.elf)
        binary = bytearray(self.elf.read_bytes())
        struct.pack_into('>Q', binary, 96, 0xffffffffffffffff)
        self.elf.write_bytes(binary)
        output = self.directory / 'should-not-exist.cpp'
        with self.assertRaises(RecompError):
            translate(self.elf, output)
        self.assertFalse(output.exists())


if __name__ == '__main__':
    unittest.main()
