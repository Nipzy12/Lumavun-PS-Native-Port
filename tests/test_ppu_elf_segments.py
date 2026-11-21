"""V0.8.4 original PPC64 ELF multi-segment + RELA regression tests."""
import pathlib
import shutil
import struct
import subprocess
import sys
import tempfile
import unittest

ROOT=pathlib.Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/"tools"))
from ppu_homebrew import create_elf
from ppu_elf_layout import parse_ppu_image, ElfLayoutError
from ppu_static_recomp import translate, RecompError


class PPUMultiSegmentTests(unittest.TestCase):
    def setUp(self):
        temp=tempfile.TemporaryDirectory()
        self.addCleanup(temp.cleanup)
        self.root=pathlib.Path(temp.name)
        self.elf=self.root/"ORIGINAL_MULTI_SEGMENT.ELF"
        create_elf(self.elf)

    def patch(self, offset, fmt, value):
        data=bytearray(self.elf.read_bytes())
        struct.pack_into(">"+fmt,data,offset,value)
        self.elf.write_bytes(data)

    def test_separate_executable_data_bss_and_rela(self):
        image=parse_ppu_image(self.elf)
        self.assertEqual(image.code_base,0x10000)
        self.assertEqual(image.entry_index,0)
        self.assertEqual(len(image.words),41)
        self.assertEqual(len(image.data_segments),1)
        data=image.data_segments[0]
        self.assertEqual(data.virtual_address,0x3000)
        self.assertEqual(len(data.contents),16)
        self.assertEqual(data.memory_size,64)
        self.assertEqual(data.contents[8],0xab)
        self.assertEqual(len(image.relocations),1)
        self.assertEqual((image.relocations[0].virtual_address,image.relocations[0].value),
                         (0x3000,0x2000))

    def test_native_relocation_and_bss_really_execute(self):
        cpp=self.root/"compiled.cpp"
        translate(self.elf,cpp)
        generated=cpp.read_text(encoding="utf-8")
        self.assertIn("guest_store(memory, UINT64_C(12288), 8, UINT64_C(8192))",generated)
        self.assertIn("memory[4104] = static_cast<uint8_t>(171)",generated)
        self.assertIn("PPU_ELF_RELOCATED=",generated)
        if not shutil.which("g++"):
            self.skipTest("Native result covered by MSVC/CTest")
        exe=self.root/"compiled"
        subprocess.run(["g++","-std=c++17","-O2",str(cpp),"-o",str(exe)],check=True)
        result=subprocess.run([str(exe)],text=True,capture_output=True,timeout=15)
        self.assertEqual(result.returncode,0,result.stderr)
        self.assertIn("PPU_ELF_RELOCATED=1",result.stdout)
        self.assertIn("PPU_STACK_RESTORED=1",result.stdout)

    def test_reject_truncated_data_segment(self):
        self.patch(120+8,"Q",0xffff)
        with self.assertRaises(ElfLayoutError):parse_ppu_image(self.elf)

    def test_reject_data_segment_overlap_code(self):
        self.patch(120+16,"Q",0x10000)
        with self.assertRaises(ElfLayoutError):parse_ppu_image(self.elf)

    def test_reject_filesz_greater_than_memsz(self):
        self.patch(120+32,"Q",128)
        with self.assertRaises(ElfLayoutError):parse_ppu_image(self.elf)

    def test_reject_unsupported_relocation_type(self):
        self.patch(0x2020+8,"Q",38)  # R_PPC64_ADDR64 unsupported
        with self.assertRaises(ElfLayoutError):parse_ppu_image(self.elf)

    def test_reject_relocation_with_nonzero_symbol(self):
        self.patch(0x2020+8,"Q",(1<<32)|22)
        with self.assertRaises(ElfLayoutError):parse_ppu_image(self.elf)

    def test_reject_relocation_into_executable(self):
        self.patch(0x2020,"Q",0x10000)
        with self.assertRaises(ElfLayoutError):parse_ppu_image(self.elf)

    def test_reject_misaligned_relocation(self):
        self.patch(0x2020,"Q",0x3001)
        with self.assertRaises(ElfLayoutError):parse_ppu_image(self.elf)

    def test_reject_malformed_section_table(self):
        self.patch(40,"Q",0xffffff)
        with self.assertRaises(ElfLayoutError):parse_ppu_image(self.elf)

    def test_failed_relocation_never_creates_output(self):
        self.patch(0x2020+8,"Q",0xdead)
        output=self.root/"should-not-exist.cpp"
        with self.assertRaises(RecompError):translate(self.elf,output)
        self.assertFalse(output.exists())

    def test_three_independent_pt_loads(self):
        blob=bytearray(self.elf.read_bytes())
        # Insert a third writable PT_LOAD header in unused program-header padding.
        struct.pack_into(">H",blob,56,3)
        struct.pack_into(">IIQQQQQQ",blob,176,
                         1,6,0x2010,0x4000,0x4000,8,16,0x10)
        blob[0x2010:0x2018]=bytes([0x55,0xaa,0,0,0,0,0,0])
        self.elf.write_bytes(blob)
        image=parse_ppu_image(self.elf)
        self.assertEqual(len(image.data_segments),2)
        self.assertEqual(image.data_segments[1].virtual_address,0x4000)
        self.assertEqual(image.data_segments[1].contents[:2],b"\x55\xaa")

if __name__=="__main__":
    unittest.main()
