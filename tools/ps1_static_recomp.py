"""Strict static translator for a small self-authored PS1 MIPS-I demonstration.

Only a known instruction subset is supported. There is no MIPS decoding
at runtime in the generated C++: instructions become native statements and
direct compiled gotos. This is NOT a commercial game recompiler or PS1 BIOS.
"""
import argparse
from dataclasses import dataclass
from pathlib import Path
import struct

class RecompError(ValueError):pass

@dataclass(frozen=True)
class Code:
    entry:int
    load_address:int
    words:tuple[int,...]

def read_psx_exe(path:Path)->Code:
    blob=Path(path).read_bytes()
    if len(blob)<2052 or blob[:8]!=b'PS-X EXE':
        raise RecompError('Not a valid PS-X EXE header')
    pc=struct.unpack_from('<I',blob,0x10)[0]
    base=struct.unpack_from('<I',blob,0x18)[0]
    size=struct.unpack_from('<I',blob,0x1c)[0]
    if size<=0 or size>1024*1024 or size%4 or size+2048!=len(blob):
        raise RecompError('Invalid text section length')
    if base<0x80010000 or pc<base or pc>=base+size or (pc-base)%4:
        raise RecompError('Entry PC outside code section')
    return Code(pc,base,struct.unpack_from(f'<{size//4}I',blob,2048))

def fmt_instruction(word:int)->str:
    op=(word>>26)&63
    rs=(word>>21)&31;rt=(word>>16)&31;rd=(word>>11)&31
    imm=word&0xffff
    sign=imm if imm<32768 else imm-65536
    def write(reg,expression):
        return f'r[{reg}] = {expression};' if reg else '/* ignored zero-register write */'
    if word==0:return '/* MIPS NOP */'
    if word==0xc:return 'if(!demo_syscall(r, selfTest)) return 0;'
    if op==0 and word&63==0x21 and (word>>6)&31==0:
        return write(rd,f'r[{rs}] + r[{rt}]')
    if op==9:return write(rt,f'r[{rs}] + static_cast<uint32_t>({sign})')
    if op==10:return write(rt,f'(static_cast<int32_t>(r[{rs}]) < {sign} ? 1u : 0u)')
    if op==13:return write(rt,f'r[{rs}] | static_cast<uint32_t>({imm})')
    if op==15 and rs==0:return write(rt,f'static_cast<uint32_t>({imm}) << 16')
    raise RecompError(f'Unsupported MIPS opcode 0x{word:08x}; no runtime fallback')

RUNTIME=r"""#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <string>
// Custom homebrew demo syscall ABI; NOT the PS1 BIOS.
static bool demo_syscall(uint32_t (&r)[32], bool selfTest) {
    switch (r[2]) {
        case 1: {
            int x=static_cast<int32_t>(r[4]);
            std::cout << "FRAME " << r[5] << " ";
            for(int i=0;i<5;i++) std::cout << (i==x ? "[X]" : (i==4 ? "[*]" : "[ ]"));
            std::cout << " score=" << r[6] << "\n";
            return true;
        }
        case 2: {
            if(selfTest) {r[2]=1;return true;}
            std::cout << "Move a/d then Enter (q quits): ";
            std::string input;
            if(!std::getline(std::cin,input)) return false;
            if(input=="q" || input=="Q") return false;
            r[2]=(input=="d" || input=="D") ? 1u :
                 ((input=="a" || input=="A") ? static_cast<uint32_t>(-1) : 0u);
            return true;
        }
        case 3:
            std::cout << "RESULT score=" << r[5] << " turns=" << r[4] << "\n";
            return false;
        default:
            std::cerr << "Unsupported demo syscall " << r[2] << "\n";
            std::exit(3);
    }
}
int main(int argc,char**argv) {
    const bool selfTest=argc==2 && std::strcmp(argv[1],"--self-test")==0;
    if(!selfTest)std::cout<<"LUMAVUN PS1 MINI GAME: collect star in 10 moves, A/D + Enter\n";
    uint32_t r[32]={};
    goto L_ENTRY;
"""

def generate_native_cpp(code:Code)->str:
    words=code.words
    entry=(code.entry-code.load_address)//4
    delay_indices=set()
    jumps={}
    for i,word in enumerate(words):
        op=word>>26
        if op in (4,5):
            if i+1>=len(words) or words[i+1]!=0:
                raise RecompError('Only NOP branch delay slots supported')
            imm=word&0xffff
            if imm>=32768:imm-=65536
            target=i+1+imm
            if target<0 or target>=len(words):
                raise RecompError('Branch outside image')
            jumps[i]=target
            delay_indices.add(i+1)
        else:
            fmt_instruction(word)  # validate full image, fail closed
    if entry in delay_indices or any(t in delay_indices for t in jumps.values()):
        raise RecompError('Cannot jump into delay slot')
    lines=[RUNTIME.replace('L_ENTRY',f'L_{entry}')]
    for i,word in enumerate(words):
        if i in delay_indices:continue
        lines.append(f'L_{i}: {{')
        if word>>26 in (4,5):
            rs=(word>>21)&31;rt=(word>>16)&31
            comp='==' if word>>26==4 else '!='
            lines.append(f'  if(r[{rs}] {comp} r[{rt}]) goto L_{jumps[i]};')
            lines.append(f'  goto L_{i+2};' if i+2<len(words) else '  return 0;')
        else:
            lines.append('  '+fmt_instruction(word))
            lines.append(f'  goto L_{i+1};' if i+1<len(words) else '  return 0;')
        lines.append('}')
    lines.append('}\n')
    return '\n'.join(lines)

def recompile_file(input_path:Path,output_path:Path):
    cpp=generate_native_cpp(read_psx_exe(input_path))
    Path(output_path).parent.mkdir(parents=True,exist_ok=True)
    Path(output_path).write_text(cpp,encoding='utf-8')

if __name__=='__main__':
    p=argparse.ArgumentParser(description='Demo-only PS-X EXE to native C++ translator')
    p.add_argument('input',type=Path)
    p.add_argument('output',type=Path)
    args=p.parse_args()
    try:recompile_file(args.input,args.output)
    except (OSError,RecompError) as exc:p.error(str(exc))
    print('Generated native C++ source:',args.output)
