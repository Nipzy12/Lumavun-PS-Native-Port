"""Read-only, bounded dependency metadata inventory for PPC64 ELF candidates.

Recognizes PT_DYNAMIC and DT_NEEDED metadata for reports. Does NOT resolve,
load, link, execute, decrypt or recompile any external dependency. This tool
is independent of the intentionally restrictive homebrew static translator.
"""
import argparse
import json
from pathlib import Path
import struct

MAX_FILE=32*1024*1024
MAX_HEADERS=128
MAX_DYNAMIC_BYTES=65536
MAX_STRINGS=64
PT_LOAD=1
PT_DYNAMIC=2


class DependencyError(ValueError):
    pass


def bounds(blob, offset, length, what):
    if offset > len(blob) or length > len(blob)-offset:
        raise DependencyError(f'{what} outside ELF file')


def scan(path):
    path=Path(path)
    if path.stat().st_size > MAX_FILE:
        raise DependencyError('ELF exceeds 32 MiB read-only metadata limit')
    blob=path.read_bytes()
    if len(blob)<64 or blob[:7]!=b'\x7fELF\x02\x02\x01':
        raise DependencyError('Requires plain big-endian PPC64 ELF, not SELF')
    (elf_type,machine,version,entry,phoff,shoff,flags,ehsize,phentsize,phnum,
     shentsize,shnum,shstrndx)=struct.unpack_from('>HHIQQQIHHHHHH',blob,16)
    if (elf_type not in (2,3) or machine!=21 or version!=1 or ehsize!=64
        or phentsize!=56 or not 1<=phnum<=MAX_HEADERS):
        raise DependencyError('Unsupported ELF class, CPU or program headers')
    bounds(blob,phoff,phnum*56,'Program headers')
    if shnum:
        if shentsize!=64 or not 1<=shnum<=MAX_HEADERS:
            raise DependencyError('Unsupported section table')
        bounds(blob,shoff,shnum*64,'Section headers')
    elif shoff:
        raise DependencyError('Unparsed extended section table not supported')

    loads=[]
    dynamics=[]
    section_counts={'dynsym':0,'dynamic':0,'rela':0,'rel':0}
    for index in range(phnum):
        ptype,pflags,offset,vaddr,paddr,filesz,memsz,align=struct.unpack_from(
            '>IIQQQQQQ',blob,phoff+index*56)
        if ptype in (PT_LOAD,PT_DYNAMIC):
            if filesz>memsz:
                raise DependencyError('Segment p_filesz exceeds p_memsz')
            bounds(blob,offset,filesz,'Segment')
        if ptype==PT_LOAD:
            if vaddr+filesz>0xffffffffffffffff:
                raise DependencyError('Virtual address overflow')
            loads.append((vaddr,filesz,offset))
        if ptype==PT_DYNAMIC:
            if filesz>MAX_DYNAMIC_BYTES or filesz%16:
                raise DependencyError('Invalid or oversized dynamic table')
            dynamics.append((offset,filesz))
    if len(dynamics)>1:
        raise DependencyError('Multiple PT_DYNAMIC tables unsupported')
    for index in range(shnum):
        (_,stype,flags,address,offset,size,link,info,alignment,entsize)=struct.unpack_from(
            '>IIQQQQIIQQ',blob,shoff+index*64)
        if stype==11:section_counts['dynsym']+=1
        elif stype==6:section_counts['dynamic']+=1
        elif stype==4:section_counts['rela']+=1
        elif stype==9:section_counts['rel']+=1
        if stype!=8 and stype!=0:
            bounds(blob,offset,size,'Section')

    dynamic_entries=[]
    if dynamics:
        offset,size=dynamics[0]
        found_null=False
        for where in range(offset,offset+size,16):
            tag,value=struct.unpack_from('>qQ',blob,where)
            dynamic_entries.append((tag,value))
            if tag==0:
                found_null=True
                break
        if not found_null:
            raise DependencyError('PT_DYNAMIC has no DT_NULL terminator')
    table=dict((tag,value) for tag,value in dynamic_entries if tag in (5,10))
    needed_offsets=[value for tag,value in dynamic_entries if tag==1]
    if len(needed_offsets)>MAX_STRINGS:
        raise DependencyError('Too many DT_NEEDED records')
    needed=[]
    if needed_offsets:
        str_va=table.get(5)
        str_sz=table.get(10)
        if str_va is None or str_sz is None or str_sz==0 or str_sz>MAX_DYNAMIC_BYTES:
            raise DependencyError('Missing or oversized dynamic string table')
        mapping=None
        for va,filesz,offset in loads:
            if str_va < va:
                continue
            delta=str_va-va
            if delta<=filesz and str_sz<=filesz-delta:
                mapping=offset+delta
                break
        if mapping is None:
            raise DependencyError('Dynamic string table has no file-backed PT_LOAD')
        bounds(blob,mapping,str_sz,'Dynamic string table')
        strings=blob[mapping:mapping+str_sz]
        for index in needed_offsets:
            if index>=len(strings):
                raise DependencyError('DT_NEEDED offset outside string table')
            terminator=strings.find(b'\0',index)
            if terminator<0:
                raise DependencyError('Unterminated DT_NEEDED')
            name=strings[index:terminator].decode('utf-8',errors='replace')
            if not name:
                raise DependencyError('Empty DT_NEEDED')
            needed.append(name)
    required=bool(dynamics or section_counts['dynsym'] or section_counts['dynamic'])
    return {
        'schema':'lumavun.ppu-dependencies.v1',
        'status':'REQUIRES_IMPORT_RESOLVER' if required else 'NO_DYNAMIC_IMPORTS_OBSERVED',
        'architecture':'PowerPC64 big-endian candidate',
        'needed':needed,
        'dynamicEntries':len(dynamic_entries),
        'sections':section_counts,
        'programHeaders':phnum,
        'resolutionAttempted':False,
        'nativeExecutableCreated':False,
        'note':'Static metadata only. No game identity verification; SELF and runtime linking unsupported.'
    }


def main():
    parser=argparse.ArgumentParser(description='Bounded PPC64 ELF dynamic-import metadata inventory')
    parser.add_argument('input',type=Path)
    parser.add_argument('--report',type=Path)
    args=parser.parse_args()
    try:
        result=scan(args.input)
        encoded=json.dumps(result,indent=2,ensure_ascii=True)+'\n'
        if args.report is not None:
            if args.report.resolve()==args.input.resolve():
                raise DependencyError('Refusing to overwrite source executable')
            args.report.parent.mkdir(parents=True,exist_ok=True)
            args.report.write_text(encoded,encoding='utf-8')
        print(encoded,end='')
    except (OSError,DependencyError,struct.error) as exc:
        parser.error(str(exc))


if __name__=='__main__':
    main()
