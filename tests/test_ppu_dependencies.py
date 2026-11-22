"""Bounded, read-only PPC64 ELF import inventory tests with synthetic inputs."""
import pathlib
import struct
import sys
import tempfile
import unittest

ROOT=pathlib.Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/"tools"))
from ppu_homebrew import create_elf
from ppu_dependency_report import scan, DependencyError, MAX_FILE
from ppu_static_recomp import generate, RecompError


class ElfDependenciesTests(unittest.TestCase):
    def setUp(self):
        tmp=tempfile.TemporaryDirectory()
        self.addCleanup(tmp.cleanup)
        self.root=pathlib.Path(tmp.name)
        self.file=self.root/"ORIGINAL_PPC64.ELF"
        create_elf(self.file)

    def add_dynamic_import(self):
        data=bytearray(self.file.read_bytes())
        data.extend(bytes(max(0,0x2250-len(data))))
        name=b'libdemo.sprx\0'
        # Preserve original SHT_RELA and synthetic code. Grow the PT_LOAD
        # file-backed RW region to expose the dynamic string table after SHT.
        struct.pack_into('>Q',data,120+32,0x100) # RW filesz
        struct.pack_into('>Q',data,120+40,0x100) # RW memsz
        data[0x20c0:0x20c0+len(name)]=name
        struct.pack_into('>H',data,56,3) # three program headers
        struct.pack_into('>IIQQQQQQ',data,176,
                         2,4,0x2200,0x3080,0x3080,64,64,8)
        for i,(tag,value) in enumerate(((1,0),(5,0x30c0),(10,len(name)),(0,0))):
            struct.pack_into('>qQ',data,0x2200+i*16,tag,value)
        self.file.write_bytes(data)

    def mutate(self, offset, fmt, value):
        data=bytearray(self.file.read_bytes())
        struct.pack_into('>'+fmt,data,offset,value)
        self.file.write_bytes(data)

    def test_plain_homebrew_has_no_dynamic_imports(self):
        report=scan(self.file)
        self.assertEqual(report['status'],'NO_DYNAMIC_IMPORTS_OBSERVED')
        self.assertEqual(report['needed'],[])
        self.assertEqual(report['sections']['rela'],1)
        self.assertFalse(report['resolutionAttempted'])
        self.assertFalse(report['nativeExecutableCreated'])

    def test_resolves_dt_needed_name_without_loading_library(self):
        self.add_dynamic_import()
        report=scan(self.file)
        self.assertEqual(report['status'],'REQUIRES_IMPORT_RESOLVER')
        self.assertEqual(report['needed'],['libdemo.sprx'])
        self.assertEqual(report['dynamicEntries'],4)
        with self.assertRaises(RecompError):
            generate(self.file) # static game builder remains fail-closed

    def test_rejects_strtab_not_backed_by_file_segment(self):
        self.add_dynamic_import()
        self.mutate(0x2200+16+8,'Q',0xdeadbeef)
        with self.assertRaisesRegex(DependencyError,'file-backed'):
            scan(self.file)

    def test_rejects_oversized_strtab(self):
        self.add_dynamic_import()
        self.mutate(0x2200+32+8,'Q',0x10001)
        with self.assertRaisesRegex(DependencyError,'string table'):
            scan(self.file)

    def test_rejects_dt_needed_without_nul_terminator(self):
        self.add_dynamic_import()
        # DT_STRSZ < length of actual name, no terminating zero.
        self.mutate(0x2200+32+8,'Q',4)
        with self.assertRaisesRegex(DependencyError,'Unterminated'):
            scan(self.file)

    def test_rejects_truncated_dynamic_table(self):
        self.add_dynamic_import()
        self.mutate(176+32,'Q',33)
        with self.assertRaises(DependencyError):
            scan(self.file)

    def test_rejects_invalid_section_table_bounds(self):
        self.mutate(40,'Q',0xffffffffff)
        with self.assertRaises(DependencyError):
            scan(self.file)

    def test_dynsym_alone_requires_resolver(self):
        self.mutate(0x2040+64+4,'I',11)
        report=scan(self.file)
        self.assertEqual(report['status'],'REQUIRES_IMPORT_RESOLVER')
        self.assertEqual(report['sections']['dynsym'],1)

    def test_rejects_oversized_input_before_read(self):
        huge=self.root/"huge.elf"
        with huge.open('wb') as f:
            f.truncate(MAX_FILE+1)
        with self.assertRaisesRegex(DependencyError,'32 MiB'):
            scan(huge)


if __name__=='__main__':
    unittest.main()
