#include "console_elf.hpp"
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>
namespace fs=std::filesystem;
static int checks=0;
static void check(bool ok,const std::string& message) {
    ++checks;
    if(!ok)throw std::runtime_error("Feasibility test failed: "+message);
}
static void put(std::vector<unsigned char>& bytes,std::size_t at,
                std::uint64_t value,unsigned count,bool little) {
    for(unsigned i=0;i<count;++i) {
        const unsigned shift=little?i:(count-1-i);
        bytes[at+i]=static_cast<unsigned char>(value>>(8*shift));
    }
}
static fs::path synthetic(const fs::path& dest,bool elf64,bool little,
                          unsigned machine,bool executable=true) {
    std::vector<unsigned char> b(512,0);
    b[0]=0x7f;b[1]='E';b[2]='L';b[3]='F';
    b[4]=elf64?2:1;b[5]=little?1:2;b[6]=1;
    put(b,16,2,2,little);
    put(b,18,machine,2,little);
    put(b,20,1,4,little);
    put(b,24,0x100000,elf64?8:4,little);
    put(b,elf64?32:28,elf64?64:52,elf64?8:4,little);
    put(b,elf64?52:40,elf64?64:52,2,little);
    put(b,elf64?54:42,elf64?56:32,2,little);
    put(b,elf64?56:44,1,2,little);
    const std::size_t ph=elf64?64:52;
    put(b,ph,1,4,little); // PT_LOAD
    if(elf64) {
        put(b,ph+4,executable?5:6,4,little);
        put(b,ph+8,256,8,little);  // file offset
        put(b,ph+16,0x100000,8,little);
        put(b,ph+32,64,8,little);
        put(b,ph+40,128,8,little);
    } else {
        put(b,ph+4,256,4,little);
        put(b,ph+8,0x100000,4,little);
        put(b,ph+16,64,4,little);
        put(b,ph+20,128,4,little);
        put(b,ph+24,executable?5:6,4,little);
    }
    std::ofstream file(dest,std::ios::binary);
    file.write(reinterpret_cast<const char*>(b.data()),
               static_cast<std::streamsize>(b.size()));
    file.close();
    return dest;
}
static void patch(const fs::path& p,std::size_t at,
                  std::uint64_t value,unsigned count,bool little) {
    std::fstream file(p,std::ios::binary|std::ios::in|std::ios::out);
    std::vector<unsigned char> b(count,0);
    put(b,0,value,count,little);
    file.seekp(static_cast<std::streamoff>(at));
    file.write(reinterpret_cast<const char*>(b.data()),
               static_cast<std::streamsize>(b.size()));
}
int main() {
    const auto base=fs::temp_directory_path()/("lumavun-v06-"+std::to_string(
        std::chrono::steady_clock::now().time_since_epoch().count()));
    fs::create_directories(base);
    const auto ps2=synthetic(base/"ps2.elf",false,true,8);
    const auto ps3=synthetic(base/"ppu.elf",true,false,21);
    const auto spu=synthetic(base/"spu.elf",false,false,23);
    const auto unsupported=synthetic(base/"x86.elf",false,true,3);
    const auto noExec=synthetic(base/"noexec.elf",true,false,21,false);
    auto a=lumavun::feasibility::probe(ps2);
    check(a.status=="STRUCTURE_VERIFIED","PS2 ELF structure");
    check(a.platform=="PS2 candidate","PS2 architecture");
    check(a.architecture=="MIPS R5900 candidate","PS2 CPU");
    check(a.segments.size()==1 && a.segments[0].bytes==64,"PS2 PT_LOAD");
    check(a.segments[0].memory==128 && a.entry==0x100000,"PS2 entry/memory");

    auto b=lumavun::feasibility::probe(ps3);
    check(b.status=="STRUCTURE_VERIFIED","PS3 PPU ELF structure");
    check(b.platform=="PS3 candidate","PS3 PPU");
    check(b.byteOrder=="Big endian","PPU byte order");
    check(b.segments.size()==1 && b.segments[0].offset==256,"PPC64 offsets");
    check(b.blockers.size()>=3,"PS3 runtime blockers");

    auto c=lumavun::feasibility::probe(spu);
    check(c.status=="STRUCTURE_VERIFIED","SPU ELF structure");
    check(c.platform=="PS3 SPU candidate","SPU architecture");
    check(lumavun::feasibility::probe(unsupported).status==
          "UNSUPPORTED_ARCHITECTURE","reject x86 ELF");
    check(lumavun::feasibility::probe(noExec).status=="INCOMPLETE_ELF",
          "reject ELF without executable segment");

    const auto self=base/"EBOOT.BIN";
    {std::ofstream out(self,std::ios::binary);const char magic[16]={
        'S','C','E',0,0,0,0,0,0,0,0,0,0,0,0,0};
      out.write(magic,sizeof(magic));}
    auto d=lumavun::feasibility::probe(self);
    check(d.status=="SELF_CONTAINER","recognize SELF container");
    check(d.platform=="PS3/Vita candidate","do not assume PS3 platform");
    check(d.detail.find("no decryption")!=std::string::npos,
          "SELF decryption not claimed");

    fs::create_directories(base/"GAME"/"PS3_GAME"/"USRDIR");
    fs::copy_file(self,base/"GAME"/"PS3_GAME"/"USRDIR"/"EBOOT.BIN");
    auto folder=lumavun::feasibility::probe(base/"GAME");
    check(folder.status=="SELF_CONTAINER","resolve PS3_GAME folder");
    check(folder.inspected.filename()=="EBOOT.BIN","select child EBOOT");
    check(lumavun::feasibility::probe(base/"not-there").status=="SOURCE_UNAVAILABLE",
          "handle missing file");

    const auto bad=synthetic(base/"bad.elf",true,false,21);
    patch(bad,32,0xfffffffffffffff0ull,8,false);
    check(lumavun::feasibility::probe(bad).status=="INVALID_ELF",
          "reject out of bounds program headers");
    patch(bad,32,64,8,false);
    patch(bad,64+32,500,8,false);
    check(lumavun::feasibility::probe(bad).status=="INVALID_ELF",
          "reject PT_LOAD file range beyond EOF");
    patch(bad,64+32,64,8,false);
    patch(bad,64+40,1,8,false);
    check(lumavun::feasibility::probe(bad).status=="INVALID_ELF",
          "reject file size larger than mapped memory");

    const auto tiny=base/"tiny.bin";
    {std::ofstream out(tiny,std::ios::binary);out.put('x');}
    check(lumavun::feasibility::probe(tiny).status=="NOT_ELF",
          "reject truncated/non ELF input");
    const auto encoded=lumavun::feasibility::json(b);
    check(encoded.find("\"nativeExecutableCreated\":false")!=std::string::npos,
          "report never claims game port");
    check(encoded.find("\"fileSize\":64")!=std::string::npos,
          "report contains verified segment ranges");
    check(encoded.find("\"PPU_RECOMPILER_UNAVAILABLE\"")!=std::string::npos,
          "report includes exact recompiler blocker");
    fs::remove_all(base);
    std::cout<<checks<<" V0.6 feasibility checks passed\n";
}
