"""V0.8.5 user-visible PPC64 semantics: mapped regions, immediate ops, and dependencies.

Runs real native machine-code translation on original, self-authored PPC64 ELF.
No commercial games, firmware or keys used.
"""
import pathlib
import shutil
import struct
import subprocess
import sys
import tempfile
import unittest

ROOT=pathlib.Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/"tools"))
from ppu_homebrew import create_elf, words
from ppu_elf_layout import parse_ppu_image, ElfLayoutError
from ppu_static_recomp import translate, RecompError


def run_native(elf, root):
    cpp=root/"translated.cpp"
    translate(elf,cpp)
    exe=root/"native"
    subprocess.run(["g++","-std=c++17","-O2",str(cpp),"-o",str(exe)],check=True)
    return subprocess.run([str(exe)],capture_output=True,text=True,timeout=12)


class V085Tests(unittest.TestCase):
    def setUp(self):
        temp=tempfile.TemporaryDirectory()
        self.addCleanup(temp.cleanup)
        self.root=pathlib.Path(temp.name)
        self.elf=self.root/"OWNED.ELF"
        create_elf(self.elf)

    def test_real_powerpc_high_low_immediate_machine_code(self):
        opcodes=[w>>26 for w in words()]
        self.assertIn(15,opcodes,"Original homebrew must execute addis/lis")
        self.assertIn(24,opcodes,"Original homebrew must execute ori")
        self.assertIn(26,opcodes,"Original homebrew must execute xori")
        output=self.root/"native.cpp"
        translate(self.elf,output)
        self.assertIn('PPU_IMMEDIATE_CHAIN=',output.read_text())

    def test_native_immediate_arithmetic_result(self):
        if not shutil.which("g++"):self.skipTest("Native result also verified in MSVC CTest")
        output=run_native(self.elf,self.root)
        self.assertEqual(output.returncode,0,output.stderr)
        self.assertIn("PPU_IMMEDIATE_CHAIN=1",output.stdout)
        self.assertIn("PPU_ELF_RELOCATED=1",output.stdout)

    def test_guest_memory_hole_rejected(self):
        if not shutil.which("g++"):self.skipTest("Native memory trap also verified in MSVC CTest")
        create_elf(self.elf,guest_pointer=0x2500)
        output=run_native(self.elf,self.root)
        self.assertEqual(output.returncode,12,output.stderr)
        self.assertNotIn("PPU_HOMEBREW_RESULT=",output.stdout)

    def test_guest_data_bss_end_rejected(self):
        if not shutil.which("g++"):self.skipTest("Native memory trap also verified in MSVC CTest")
        create_elf(self.elf,guest_pointer=0x3040)
        output=run_native(self.elf,self.root)
        self.assertEqual(output.returncode,12,output.stderr)

    def test_loader_rejects_guest_segment_overlapping_demo_stack(self):
        blob=bytearray(self.elf.read_bytes())
        # RW PT_LOAD virtual address at PHDR #1 (file offset 120 + 16).
        struct.pack_into(">Q",blob,120+16,0x6000)
        self.elf.write_bytes(blob)
        with self.assertRaises(ElfLayoutError):
            parse_ppu_image(self.elf)

    def test_loader_rejects_guest_segment_overlapping_demo_scratch(self):
        blob=bytearray(self.elf.read_bytes())
        struct.pack_into(">Q",blob,120+16,0x2000)
        self.elf.write_bytes(blob)
        with self.assertRaises(ElfLayoutError):
            parse_ppu_image(self.elf)

    def test_dynamic_symbols_explicitly_rejected(self):
        blob=bytearray(self.elf.read_bytes())
        # The existing RELA section is replaced with a fake SHT_DYNSYM to
        # simulate an ELF that requires unresolved symbol/dependency handling.
        struct.pack_into(">I",blob,0x2040+64+4,11)
        self.elf.write_bytes(blob)
        with self.assertRaisesRegex(ElfLayoutError,"dynamic|symbol|import"):
            parse_ppu_image(self.elf)

    def test_dynamic_imports_fail_before_codegen(self):
        blob=bytearray(self.elf.read_bytes())
        struct.pack_into(">I",blob,0x2040+64+4,11)
        self.elf.write_bytes(blob)
        output=self.root/"should-not-be-written.cpp"
        with self.assertRaises(RecompError):
            translate(self.elf,output)
        self.assertFalse(output.exists())


if __name__=="__main__":
    unittest.main()
