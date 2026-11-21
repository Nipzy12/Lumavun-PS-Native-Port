"""V0.8.3 PPC64 nested-call, LR save/restore and checked stack-frame tests.
No PS3 game binaries or firmware are required.
"""
import pathlib
import shutil
import struct
import subprocess
import sys
import tempfile
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
from ppu_homebrew import create_elf, words
from ppu_static_recomp import RecompError, generate, translate


def overwrite_word(file, index, value):
    blob = bytearray(file.read_bytes())
    struct.pack_into(">I", blob, 0x1000 + 4 * index, value)
    file.write_bytes(blob)


def compile_and_run(cpp, directory):
    executable = directory / "translated_game"
    subprocess.run(["g++", "-std=c++17", "-O2", str(cpp), "-o", str(executable)], check=True)
    return subprocess.run([str(executable)], text=True, capture_output=True, timeout=12)


class NestedPPUCallTests(unittest.TestCase):
    def setUp(self):
        temp = tempfile.TemporaryDirectory()
        self.addCleanup(temp.cleanup)
        self.root = pathlib.Path(temp.name)
        self.elf = self.root / "PPU_STACK.ELF"
        create_elf(self.elf)

    def test_original_elf_has_two_calls_and_stack_frame(self):
        opcodes = words()
        linked_calls = [i for i, word in enumerate(opcodes) if (word >> 26) == 18 and (word & 1)]
        self.assertEqual(linked_calls, [2, 12])
        self.assertEqual(opcodes[6] & 0xfc000003, 0xf8000001)  # stdu
        self.assertEqual((opcodes[5] >> 1) & 0x3ff, 339)       # mflr / mfspr
        self.assertEqual((opcodes[14] >> 1) & 0x3ff, 467)      # mtlr / mtspr
        self.assertEqual(opcodes[17] & 0xfc000003, 0xf8000001)

    def test_translated_elf_restores_sp_and_link_register(self):
        cpp = self.root / "nested.cpp"
        translate(self.elf, cpp)
        source = cpp.read_text(encoding="utf-8")
        self.assertIn("guest_store(memory", source)
        self.assertIn("lr = r[12];", source)
        self.assertIn("r[12] = lr;", source)
        self.assertIn("PPU_STACK_RESTORED=", source)
        if not shutil.which("g++"):
            self.skipTest("Native execution checked separately with MSVC in CTest")
        process = compile_and_run(cpp, self.root)
        self.assertEqual(process.returncode, 0, process.stderr)
        self.assertEqual(process.stdout.strip().splitlines(), [
            "PPU_HOMEBREW_RESULT=15",
            "PPU_GUEST_BE_BYTES=18,52",
            "PPU_STACK_RESTORED=1",
        ])

    def test_stdu_rejects_invalid_base_register(self):
        original = words()[6]
        overwrite_word(self.elf, 6, original & ~(31 << 16))  # RA -> zero
        with self.assertRaises(RecompError):
            generate(self.elf)

    def test_reject_non_lr_special_register_move(self):
        original = words()[5]
        overwrite_word(self.elf, 5, (original & ~(31 << 16)) | (1 << 16))  # SPR=1 XER
        with self.assertRaises(RecompError):
            generate(self.elf)

    def test_stack_frame_outside_guest_ram_traps_natively(self):
        # r1 starts at 0x7000; stdu -0x6000 would target 0x1000, outside RAM.
        original = words()[6]
        overwrite_word(self.elf, 6, (original & 0xffff0003) | 0xa000)
        cpp = self.root / "bad_stack.cpp"
        translate(self.elf, cpp)
        if not shutil.which("g++"):
            self.skipTest("Native exit-code validation checked separately on Windows")
        result = compile_and_run(cpp, self.root)
        self.assertEqual(result.returncode, 12)

    def test_unaligned_stack_update_traps_natively(self):
        # r1=0x7000; stdu -28 creates a 4-byte-aligned but non-8-aligned EA.
        original = words()[6]
        overwrite_word(self.elf, 6, (original & 0xffff0003) | 0xffe4)
        cpp = self.root / "bad_frame.cpp"
        translate(self.elf, cpp)
        if not shutil.which("g++"):
            self.skipTest("Native exit-code validation checked separately on Windows")
        result = compile_and_run(cpp, self.root)
        self.assertEqual(result.returncode, 12)


if __name__ == "__main__":
    unittest.main()
