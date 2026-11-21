"""Build a self-authored PS-X EXE containing real MIPS-I mini-game instructions.

The host bridge is a purpose-built demo syscall ABI, NOT the PlayStation BIOS.
No copyrighted game, firmware, or commercial assets are required.
"""
import argparse
import pathlib
import struct

PC = 0x80010000
REG = {'zero':0,'v0':2,'a0':4,'a1':5,'a2':6,'t0':8,'s0':16,'s1':17,'s2':18}

PROGRAM = [
 ('addiu','s0','zero',0), ('addiu','s1','zero',0), ('addiu','s2','zero',0),
 ('label','loop'),
 ('addiu','v0','zero',1),
 ('addu','a0','s0','zero'),('addu','a1','s1','zero'),('addu','a2','s2','zero'),
 ('syscall',),
 ('addiu','v0','zero',2), ('syscall',), ('addu','s0','s0','v0'),
 ('slti','t0','s0',0), ('beq','t0','zero','skip_neg'), ('nop',),
 ('addu','s0','zero','zero'), ('label','skip_neg'),
 ('slti','t0','s0',5), ('bne','t0','zero','check_score'), ('nop',),
 ('addiu','s0','zero',4), ('label','check_score'),
 ('addiu','t0','zero',4), ('bne','s0','t0','no_score'), ('nop',),
 ('addiu','s2','s2',1),('addiu','s0','zero',0), ('label','no_score'),
 ('addiu','s1','s1',1), ('slti','t0','s1',10), ('bne','t0','zero','loop'), ('nop',),
 ('addiu','v0','zero',3),('addu','a0','s1','zero'),('addu','a1','s2','zero'),
 ('syscall',)
]

def compile_program():
    labels={}
    index=0
    for item in PROGRAM:
        if item[0]=='label':
            if item[1] in labels:raise ValueError('duplicate label')
            labels[item[1]]=index
        else:index+=1
    words=[]
    for item in PROGRAM:
        name=item[0]
        if name=='label':continue
        if name=='nop':word=0
        elif name=='syscall':word=0xc
        elif name=='addu':
            _,rd,rs,rt=item
            word=(REG[rs]<<21)|(REG[rt]<<16)|(REG[rd]<<11)|0x21
        elif name in ('addiu','slti'):
            _,rt,rs,imm=item
            word=((9 if name=='addiu' else 10)<<26)|(REG[rs]<<21)|(REG[rt]<<16)|(imm&0xffff)
        elif name in ('beq','bne'):
            _,rs,rt,target=item
            distance=labels[target]-(len(words)+1)
            if not -32768<=distance<=32767:raise ValueError('branch out of range')
            word=((4 if name=='beq' else 5)<<26)|(REG[rs]<<21)|(REG[rt]<<16)|(distance&0xffff)
        else:raise ValueError(name)
        words.append(word)
    return words

def make_demo(output:pathlib.Path):
    words=compile_program()
    if len(words)>512:raise ValueError('demo too large')
    header=bytearray(2048);header[:8]=b'PS-X EXE'
    struct.pack_into('<I',header,0x10,PC)
    struct.pack_into('<I',header,0x18,PC)
    struct.pack_into('<I',header,0x1c,2048)
    output.parent.mkdir(parents=True,exist_ok=True)
    output.write_bytes(header+b''.join(struct.pack('<I',w) for w in words)+b'\0'*(2048-len(words)*4))
    return len(words)

if __name__=='__main__':
    p=argparse.ArgumentParser()
    p.add_argument('output',type=pathlib.Path)
    a=p.parse_args()
    print('Created self-authored PS-X EXE, instruction count:',make_demo(a.output))
