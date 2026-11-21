import pathlib
import shutil
import subprocess
import sys
import tempfile
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
from ppu_homebrew import create_elf
from ppu_static_recomp import translate, RecompError


class PPUStaticRecompilerTest(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.root = pathlib.Path(self.tmp.name)
        self.elf = self.root / 'DEMO_PPU.ELF'
        create_elf(self.elf)

    def test_ppu_elf_translates_and_executes_native_result(self):
        cpp = self.root / 'native.cpp'
        translate(self.elf, cpp)
        generated = cpp.read_text(encoding='utf-8')
        self.assertIn('goto L_5;', generated)
        self.assertIn('PPU_HOMEBREW_RESULT=', generated)
        self.assertNotIn('decode_powerpc', generated)
        if shutil.which('g++'):
            binary = self.root / 'native'
            subprocess.run(['g++', '-std=c++17', '-O2', str(cpp), '-o', str(binary)], check=True)
            process = subprocess.run([str(binary)], text=True, capture_output=True, timeout=10)
            self.assertEqual(process.returncode, 0, process.stderr)
            self.assertEqual(process.stdout.strip().splitlines(),
                             ['PPU_HOMEBREW_RESULT=15', 'PPU_GUEST_BE_BYTES=18,52',
                              'PPU_STACK_RESTORED=1'])

    def test_rejects_unrecognized_opcode_without_output(self):
        data = bytearray(self.elf.read_bytes())
        data[0x1000:0x1004] = (0x7C000000).to_bytes(4, 'big')
        self.elf.write_bytes(data)
        cpp = self.root / 'should_not_exist.cpp'
        with self.assertRaises(RecompError):
            translate(self.elf, cpp)
        self.assertFalse(cpp.exists())

    def test_rejects_wrong_machine_without_output(self):
        data = bytearray(self.elf.read_bytes())
        data[18:20] = (62).to_bytes(2, 'big')
        self.elf.write_bytes(data)
        cpp = self.root / 'output.cpp'
        with self.assertRaises(RecompError):
            translate(self.elf, cpp)
        self.assertFalse(cpp.exists())

    def test_rejects_out_of_segment_branch(self):
        data = bytearray(self.elf.read_bytes())
        data[0x1000 + 8:0x1000 + 12] = (0x48001001).to_bytes(4, 'big')
        self.elf.write_bytes(data)
        with self.assertRaises(RecompError):
            translate(self.elf, self.root / 'out.cpp')

    def test_rejects_truncated_segment(self):
        self.elf.write_bytes(self.elf.read_bytes()[:0x1010])
        with self.assertRaises(RecompError):
            translate(self.elf, self.root / 'out.cpp')

    def test_refuses_source_output_path_collision(self):
        with self.assertRaises(RecompError):
            translate(self.elf, self.elf)
        self.assertEqual(self.elf.read_bytes()[:4], b'\x7fELF')

    def test_generated_native_has_instruction_budget(self):
        cpp = self.root / 'source.cpp'
        translate(self.elf, cpp)
        self.assertIn('if (++steps > UINT64_C(1000000)) return 10;', cpp.read_text())

    def test_rejects_self_container(self):
        self.elf.write_bytes(b'SCE\x00' + bytes(100))
        with self.assertRaises(RecompError):
            translate(self.elf, self.root / 'out.cpp')


if __name__ == '__main__':
    unittest.main()
