"""V0.8.6 PPC64 CR0 LT/GT/EQ and BO/BI branch verification on native code.

Exercises the ORIGINAL self-authored ELF after static translation, not PS3 CellOS.
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
from ppu_static_recomp import RecompError, generate, translate


class ConditionRegisterTests(unittest.TestCase):
    def setUp(self):
        temp=tempfile.TemporaryDirectory()
        self.addCleanup(temp.cleanup)
        self.root=pathlib.Path(temp.name)
        self.elf=self.root/"OWNED_CR.ELF"
        create_elf(self.elf)

    def test_original_elf_contains_signed_unsigned_and_condition_branches(self):
        machine_code=words()
        ops=[w>>26 for w in machine_code]
        self.assertIn(10,ops,"CMPLWI must be encoded as real PowerPC opcode 10")
        self.assertIn(11,ops,"CMPWI must be encoded as real PowerPC opcode 11")
        branches=[w for w in machine_code if (w>>26)==16]
        self.assertTrue(any(((w>>21)&31)==12 and ((w>>16)&31)==0 for w in branches),
                        "BLT must test CR0 LT")
        self.assertTrue(any(((w>>21)&31)==12 and ((w>>16)&31)==1 for w in branches),
                        "BGT must test CR0 GT")
        self.assertTrue(any(((w>>21)&31)==12 and ((w>>16)&31)==2 for w in branches),
                        "BEQ must test CR0 EQ")
        self.assertTrue(any(((w>>21)&31)==4 and ((w>>16)&31)==2 for w in branches),
                        "BNE must test CR0 EQ false")

    def test_native_condition_register_path_is_executed(self):
        generated=self.root/"native.cpp"
        translate(self.elf,generated)
        source=generated.read_text(encoding="utf-8")
        self.assertIn("PPU_CR0_BRANCHES=",source)
        self.assertIn("cr0 &",source)
        if not shutil.which("g++"):
            self.skipTest("Native execution also tested via MSVC in Windows CTest")
        executable=self.root/"native"
        subprocess.run(["g++","-std=c++17","-O2",str(generated),"-o",str(executable)],
                       check=True)
        proc=subprocess.run([str(executable)],text=True,capture_output=True,timeout=10)
        self.assertEqual(proc.returncode,0,proc.stderr)
        self.assertIn("PPU_CR0_BRANCHES=1",proc.stdout)
        self.assertIn("PPU_IMMEDIATE_CHAIN=1",proc.stdout)

    def test_bad_condition_register_field_is_rejected(self):
        binary=bytearray(self.elf.read_bytes())
        word=struct.unpack_from(">I",binary,0x1000+10*4)[0] # original CMPWI
        struct.pack_into(">I",binary,0x1000+10*4,word|(1<<23)) # CR1 unsupported
        self.elf.write_bytes(binary)
        with self.assertRaises(RecompError):generate(self.elf)

    def test_unsupported_counter_branch_option_is_rejected(self):
        binary=bytearray(self.elf.read_bytes())
        word=struct.unpack_from(">I",binary,0x1000+11*4)[0] # original BNE
        struct.pack_into(">I",binary,0x1000+11*4,
                         (word&~(31<<21))|(16<<21)) # CTR-dependent BO
        self.elf.write_bytes(binary)
        with self.assertRaises(RecompError):generate(self.elf)

    def test_unsupported_absolute_branch_is_rejected(self):
        binary=bytearray(self.elf.read_bytes())
        word=struct.unpack_from(">I",binary,0x1000+11*4)[0]
        struct.pack_into(">I",binary,0x1000+11*4,word|2) # AA
        self.elf.write_bytes(binary)
        with self.assertRaises(RecompError):generate(self.elf)


if __name__=="__main__":
    unittest.main()
