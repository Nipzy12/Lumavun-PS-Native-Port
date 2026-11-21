import pathlib
import shutil
import subprocess
import sys
import tempfile
import unittest

ROOT=pathlib.Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools'))
from make_demo_ps1 import make_demo
from ps1_static_recomp import read_psx_exe,generate_native_cpp,recompile_file,RecompError

class NativeRecompTests(unittest.TestCase):
    def setUp(self):
        self.tmp=tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.root=pathlib.Path(self.tmp.name)
        self.input=self.root/'MINIGAME.EXE'
        make_demo(self.input)

    def test_psx_header_has_real_mips_instructions(self):
        code=read_psx_exe(self.input)
        self.assertEqual(code.entry,0x80010000)
        self.assertEqual(code.load_address,0x80010000)
        self.assertIn(0xc,code.words)
        self.assertGreater(len(code.words),25)

    def test_generated_native_program_executes_original_logic(self):
        generated=generate_native_cpp(read_psx_exe(self.input))
        self.assertIn('goto L_',generated)
        self.assertIn('demo_syscall',generated)
        self.assertNotIn('decode_mips',generated)
        self.assertIn('int main(',generated)
        if shutil.which('g++'):
            cpp=self.root/'output.cpp'
            cpp.write_text(generated)
            binary=self.root/'output'
            subprocess.run(['g++','-std=c++17','-O2',str(cpp),'-o',str(binary)],check=True)
            run=subprocess.run([str(binary),'--self-test'],capture_output=True,text=True,timeout=15)
            self.assertEqual(run.returncode,0,run.stderr)
            self.assertIn('RESULT score=2 turns=10',run.stdout)
            self.assertEqual(run.stdout.count('FRAME '),10)

    def test_win32_translation_reuses_compiled_mips_control_flow(self):
        code=read_psx_exe(self.input)
        gui=generate_native_cpp(code,'win32')
        console=generate_native_cpp(code)
        self.assertIn('wWinMain(',gui)
        self.assertIn('native_game_win.hpp',gui)
        self.assertIn('demo_syscall(r, selfTest)',gui)
        self.assertEqual(gui.count('goto L_'),console.count('goto L_'))
        self.assertNotIn('decode_mips',gui)

    def test_reject_unknown_native_runtime(self):
        with self.assertRaises(RecompError):
            generate_native_cpp(read_psx_exe(self.input),'unsupported-host')

    def test_wrong_magic_rejected(self):
        blob=bytearray(self.input.read_bytes());blob[:8]=b'BADMAGIC'
        self.input.write_bytes(blob)
        with self.assertRaises(RecompError):read_psx_exe(self.input)

    def test_invalid_entry_rejected(self):
        blob=bytearray(self.input.read_bytes());blob[0x10:0x14]=(0).to_bytes(4,'little')
        self.input.write_bytes(blob)
        with self.assertRaises(RecompError):read_psx_exe(self.input)

    def test_unsupported_load_word_opcode_rejected(self):
        blob=bytearray(self.input.read_bytes());blob[0x800:0x804]=(0x8E080000).to_bytes(4,'little')
        self.input.write_bytes(blob)
        with self.assertRaises(RecompError):generate_native_cpp(read_psx_exe(self.input))

    def test_invalid_branch_target_rejected(self):
        blob=bytearray(self.input.read_bytes());blob[0x800:0x804]=(0x10008000).to_bytes(4,'little')
        self.input.write_bytes(blob)
        with self.assertRaises(RecompError):generate_native_cpp(read_psx_exe(self.input))

    def test_invalid_image_never_creates_output(self):
        target=self.root/'output.cpp'
        blob=bytearray(self.input.read_bytes());blob[0x800:0x804]=(0x8E080000).to_bytes(4,'little')
        self.input.write_bytes(blob)
        with self.assertRaises(RecompError):recompile_file(self.input,target)
        self.assertFalse(target.exists())

if __name__=='__main__':
    unittest.main()
