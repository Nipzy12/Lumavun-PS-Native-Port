"""Black-box integration tests for PPC64 guest-memory operations.

Synthetic, owned ELF only. No PS3 firmware or commercial game.
"""
import pathlib
import shutil
import struct
import subprocess
import sys
import tempfile
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
from ppu_homebrew import create_elf
from ppu_static_recomp import generate, translate, RecompError


def guest_program(path, guest_pointer=0x2000, ds_suffix=0):
    """Use the complete original nested-call fixture, modifying a test-only opcode."""
    create_elf(path, guest_pointer=guest_pointer)
    if ds_suffix:
        binary = bytearray(path.read_bytes())
        # Index 19 is helper's std r4,0(r5). DS form suffix 2/3 is unsupported.
        offset = 0x1000 + 19 * 4
        word = struct.unpack_from('>I', binary, offset)[0]
        struct.pack_into('>I', binary, offset, word | ds_suffix)
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
        if not shutil.which('g++'):
            self.skipTest('Native execution covered by CMake target on Windows')
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
        guest_program(self.elf, ds_suffix=2)
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
