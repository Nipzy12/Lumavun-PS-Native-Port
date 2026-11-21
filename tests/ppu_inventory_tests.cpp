#include "ppu_code_inventory.hpp"
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>
namespace fs=std::filesystem;
using lumavun::ppu::scan;
static int tests=0;
static void verify(bool condition,const char* label){
    ++tests;
    if(!condition)throw std::runtime_error(std::string("PPU inventory check failed: ")+label);
}
static void be(std::vector<unsigned char>& b,std::size_t offset,std::uint64_t value,unsigned count){
    for(unsigned i=0;i<count;++i)b[offset+i]=static_cast<unsigned char>(value>>(8*(count-1-i)));
}
static fs::path ppuFixture(const fs::path& out) {
    std::vector<unsigned char> b(512,0);
    b[0]=0x7f;b[1]='E';b[2]='L';b[3]='F';b[4]=2;b[5]=2;b[6]=1;
    be(b,16,2,2);be(b,18,21,2);be(b,20,1,4);
    be(b,24,0x100000,8);be(b,32,64,8);
    be(b,52,64,2);be(b,54,56,2);be(b,56,1,2);
    be(b,64,1,4);be(b,68,5,4);
    be(b,72,256,8);be(b,80,0x100000,8);
    be(b,96,32,8);be(b,104,64,8);
    const std::uint32_t instructions[] = {
      0x48000009u, // bl +8 -> internal call candidate
      0x41820008u, // bc +8 -> conditional
      0x48000008u, // b +8
      0x4e800020u, // blr
      0x48000001u, // bl +0 -> internal candidate
      0x4e800420u, // bcctr
      0x48000001u, // bl +0 -> internal candidate
      0x60000000u  // ori r0,r0,0
    };
    for(std::size_t i=0;i<8;++i)be(b,256+i*4,instructions[i],4);
    std::ofstream f(out,std::ios::binary|std::ios::trunc);
    f.write(reinterpret_cast<const char*>(b.data()),
            static_cast<std::streamsize>(b.size()));
    return out;
}
static void patch(const fs::path& p,std::size_t index,std::uint32_t word){
    std::vector<unsigned char> b(4);
    be(b,0,word,4);
    std::fstream f(p,std::ios::binary|std::ios::in|std::ios::out);
    f.seekp(static_cast<std::streamoff>(256+4*index));
    f.write(reinterpret_cast<const char*>(b.data()),4);
}
int main(){
    auto root=fs::temp_directory_path()/(
        "lumavun-ppu-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    fs::create_directories(root);
    const auto ppu=ppuFixture(root/"ppu.elf");
    auto p=scan(ppu);
    verify(p.status=="SCAN_COMPLETE","complete verified ELF scan");
    verify(p.elf.platform=="PS3 candidate","architecture PS3 PPU");
    verify(p.executableBytes==32&&p.bytesScanned==32,"only executable segment bytes scanned");
    verify(p.instructionWords==8,"PPC word count");
    verify(p.directBranches==4,"direct branch count");
    verify(p.conditionalBranches==1,"conditional branch count");
    verify(p.indirectBranches==2,"LR and CTR indirect branch count");
    verify(p.linkBranches==3,"link branch count");
    verify(p.linkTargetsWithinImage==3&&p.linkTargetsOutsideImage==0,
           "internal link-target classifications");
    verify(p.candidateTargetSamples.size()==3,"three distinct candidate locations");
    verify(p.candidateTargetSamples.front()==0x100008,"bl +8 target");
    verify(p.candidateTargetSamples.back()==0x100018,"last internal call target");
    verify(p.blockers.size()==4,"unimplemented runtime blockers preserved");
    const auto json=lumavun::ppu::json(p);
    verify(json.find("\"nativeExecutableCreated\":false")!=std::string::npos,
           "never claim native port");
    verify(json.find("\"candidateTargetsVerifiedAsFunctions\":false")!=std::string::npos,
           "cannot assert function boundary status");
    verify(json.find("\"instructionWords\":8")!=std::string::npos,
           "machine readable counts");

    auto limited=scan(ppu,16);
    verify(limited.status=="PARTIAL_SCAN"&&limited.partial,"budget enforces partial status");
    verify(limited.bytesScanned==16&&limited.instructionWords==4,
           "budget actually limits instruction reads");
    verify(scan(ppu,0).status=="INVALID_SCAN_BUDGET","reject zero scan budget");
    verify(scan(ppu,lumavun::ppu::kMaximumScanBudget+1).status==
           "INVALID_SCAN_BUDGET","reject unbounded scans");

    patch(ppu,0,0x48000101u); // bl to virtual address outside mapped executable range
    auto external=scan(ppu);
    verify(external.linkTargetsWithinImage==2,"two remaining in-image call candidates");
    verify(external.linkTargetsOutsideImage==1,"external linked branch counted");
    patch(ppu,0,0x48000009u);
    patch(ppu,1,0x41820009u); // conditional linked branch
    auto conditional=scan(ppu);
    verify(conditional.linkBranches==4&&conditional.candidateTargetSamples.size()==4,
           "conditional linked branch counted as candidate");

    const auto negative=lumavun::ppu::branchTarget(0x4bfffffdu,0x100020,false);
    verify(negative==0x10001c,"signed negative direct branch displacement");
    verify(lumavun::ppu::branchTarget(0x4182fffcu,0x100010,true)==0x10000c,
           "signed negative conditional branch displacement");
    verify(lumavun::ppu::branchTarget(0x48000003u,0x100020,false)==0,
           "absolute branch does not add instruction PC");

    auto self=root/"EBOOT.BIN";
    {std::ofstream f(self,std::ios::binary);const char magic[4]={'S','C','E',0};f.write(magic,4);}
    const auto signedContainer=scan(self);
    verify(signedContainer.status=="NEEDS_LAWFUL_UNPACKED_ELF","SELF is not implicitly decrypted");
    verify(signedContainer.instructionWords==0,"SELF never scanned as plaintext");
    verify(scan(root/"missing.bin").status=="ELF_NOT_VERIFIED","missing input fails closed");

    fs::remove_all(root);
    std::cout<<tests<<" PPU inventory validation checks passed\n";
}
