#pragma once
// Read-only PPC64 PPU branch inventory for a structurally valid PS3-candidate ELF.
// No instruction execution, no SELF decryption, no native code translation.
// Branch-and-link targets are candidates, NOT authenticated function boundaries.
#include "console_elf.hpp"
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <limits>
#include <set>
#include <sstream>
#include <string>
#include <vector>

namespace lumavun::ppu {
namespace fs=std::filesystem;
constexpr std::uint64_t kDefaultScanBudget=16ull*1024*1024;
constexpr std::uint64_t kMaximumScanBudget=64ull*1024*1024;
constexpr std::size_t kMaxTargetSamples=64;

struct Inventory {
    feasibility::Report elf;
    std::string status="UNSUPPORTED_INPUT";
    std::string detail="A structurally valid big-endian PPC64 ELF file is required.";
    std::uint64_t executableBytes=0;
    std::uint64_t bytesScanned=0;
    std::uint64_t instructionWords=0;
    std::uint64_t directBranches=0;      // primary op 18
    std::uint64_t conditionalBranches=0; // primary op 16
    std::uint64_t linkBranches=0;        // direct b(l) or bc(l)
    std::uint64_t indirectBranches=0;    // bclr / bcctr family
    std::uint64_t linkTargetsWithinImage=0;
    std::uint64_t linkTargetsOutsideImage=0;
    std::vector<std::uint64_t> candidateTargetSamples;
    bool sampleTruncated=false;
    bool partial=false;
    std::vector<std::string> blockers;
};
inline bool containsCodeAddress(const feasibility::Report& r,std::uint64_t target) {
    for (const auto& s:r.segments) {
        if(!(s.flags&1) || s.bytes==0)continue;
        if(target>=s.vaddr && target-s.vaddr<s.bytes)return true;
    }
    return false;
}
inline std::int64_t branchDisplacement(std::uint32_t word,bool conditional) {
    if(conditional) {
        std::int64_t n=word & 0x0000fffcu;
        return (n&0x8000) ? n-0x10000 : n;
    }
    std::int64_t n=word & 0x03fffffcu;
    return (n&0x02000000) ? n-0x04000000 : n;
}
inline std::uint64_t branchTarget(std::uint32_t word,std::uint64_t pc,bool conditional) {
    const auto displacement=branchDisplacement(word,conditional);
    if(word & 2) return static_cast<std::uint64_t>(displacement); // AA=1
    return pc + static_cast<std::uint64_t>(displacement);          // 64-bit wrap per address arithmetic
}
inline Inventory scan(const fs::path& input,std::uint64_t budget=kDefaultScanBudget) {
    Inventory out;out.elf=feasibility::probe(input);
    if(out.elf.status=="SELF_CONTAINER") {
        out.status="NEEDS_LAWFUL_UNPACKED_ELF";
        out.detail="SELF container detected; no decryption/unpacking or PPC scanning attempted.";
        out.blockers={"SELF_UNPACK_REQUIRED","PPU_CODE_UNAVAILABLE"};
        return out;
    }
    if(out.elf.status!="STRUCTURE_VERIFIED") {
        out.status="ELF_NOT_VERIFIED";
        out.detail="ELF structure did not pass the existing bounded validator.";
        out.blockers={"VALIDATED_PPU_ELF_REQUIRED"};
        return out;
    }
    if(out.elf.platform!="PS3 candidate"||out.elf.architecture!="PowerPC64 PPU candidate") {
        out.status="NOT_PPU_ELF";
        out.detail="Input is not a big-endian ELF64 PowerPC PPU candidate.";
        out.blockers={"PPU_ELF_REQUIRED"};
        return out;
    }
    if(budget<4||budget>kMaximumScanBudget) {
        out.status="INVALID_SCAN_BUDGET";
        out.detail="Scan budget must be between 4 bytes and 64 MiB.";
        out.blockers={"INVALID_SCAN_BUDGET"};
        return out;
    }
    std::ifstream file(out.elf.inspected,std::ios::binary);
    if(!file) {
        out.status="READ_ERROR";out.detail="Cannot reopen ELF for scanning.";
        out.blockers={"READ_ERROR"};return out;
    }
    for(const auto& segment:out.elf.segments) {
        if(!(segment.flags&1))continue;
        if(segment.vaddr>std::numeric_limits<std::uint64_t>::max()-segment.bytes) {
            out.status="INVALID_VIRTUAL_ADDRESS";
            out.detail="Executable virtual address span overflows 64-bit address space.";
            out.blockers={"INVALID_VIRTUAL_ADDRESS"};return out;
        }
        if(out.executableBytes>std::numeric_limits<std::uint64_t>::max()-segment.bytes) {
            out.status="INVALID_SIZE";
            out.blockers={"INVALID_SIZE"};return out;
        }
        out.executableBytes+=segment.bytes;
    }
    std::set<std::uint64_t> candidates;
    std::array<unsigned char,4096> data{};
    for(const auto& segment:out.elf.segments) {
        if(!(segment.flags&1)||segment.bytes==0)continue;
        if((segment.offset%4)!=0||(segment.vaddr%4)!=0||
           (segment.bytes%4)!=0)out.partial=true;
        std::uint64_t left=segment.bytes-(segment.bytes%4);
        std::uint64_t current=0;
        while(left>=4 && out.bytesScanned+4<=budget) {
            const auto size=static_cast<std::size_t>(
                std::min<std::uint64_t>({left,budget-out.bytesScanned,data.size()}))&~std::size_t(3);
            if(size==0)break;
            file.clear();
            file.seekg(static_cast<std::streamoff>(segment.offset+current));
            file.read(reinterpret_cast<char*>(data.data()),
                      static_cast<std::streamsize>(size));
            if(!file) {
                out.status="READ_ERROR";
                out.detail="Executable PT_LOAD contents could not be read.";
                out.blockers={"READ_ERROR"};return out;
            }
            for(std::size_t j=0;j<size;j+=4) {
                const auto word=static_cast<std::uint32_t>(feasibility::number(data.data()+j,0,4,false));
                const auto op=word>>26;
                const auto pc=segment.vaddr+current+j;
                ++out.instructionWords;
                if(op==18 || op==16) {
                    const bool conditional=op==16;
                    if(conditional)++out.conditionalBranches;
                    else ++out.directBranches;
                    if(word&1) {
                        ++out.linkBranches;
                        const auto dest=branchTarget(word,pc,conditional);
                        if(containsCodeAddress(out.elf,dest)) {
                            ++out.linkTargetsWithinImage;
                            if(candidates.find(dest)==candidates.end()) {
                                if(candidates.size()<kMaxTargetSamples)candidates.insert(dest);
                                else out.sampleTruncated=true;
                            }
                        }else ++out.linkTargetsOutsideImage;
                    }
                }else if(op==19) {
                    const auto xo=(word>>1)&0x3ffu;
                    if(xo==16||xo==528)++out.indirectBranches;
                }
            }
            current+=size;
            left-=size;
            out.bytesScanned+=size;
        }
        if(current<segment.bytes)out.partial=true;
    }
    out.candidateTargetSamples.assign(candidates.begin(),candidates.end());
    if(out.bytesScanned<out.executableBytes)out.partial=true;
    out.status=out.partial?"PARTIAL_SCAN":"SCAN_COMPLETE";
    out.detail=out.partial
      ? "Only part of the executable bytes could be scanned under alignment/budget limits."
      : "PPC64 instruction words counted in executable PT_LOAD data. Not a control-flow graph or verified function inventory.";
    out.blockers={
        "GAME_IDENTITY_UNVERIFIED",
        "FUNCTION_BOUNDARIES_UNVERIFIED",
        "PPU_RECOMPILER_NOT_IMPLEMENTED",
        "SPU_RSX_CELLOS_RUNTIME_UNAVAILABLE"
    };
    if(out.partial)out.blockers.insert(out.blockers.begin(),"PARTIAL_CODE_COVERAGE");
    return out;
}
inline std::string json(const Inventory& r) {
    std::ostringstream o;
    o<<"{\n\"schema\":\"lumavun.ppu-code-inventory.v1\","
     <<"\n\"source\":\""<<lumavun::quote(r.elf.inspected.u8string())<<"\","
     <<"\n\"status\":\""<<lumavun::quote(r.status)<<"\","
     <<"\n\"detail\":\""<<lumavun::quote(r.detail)<<"\","
     <<"\n\"fileBytes\":"<<r.elf.fileBytes<<","
     <<"\n\"executableBytes\":"<<r.executableBytes<<","
     <<"\n\"bytesScanned\":"<<r.bytesScanned<<","
     <<"\n\"instructionWords\":"<<r.instructionWords<<","
     <<"\n\"directBranches\":"<<r.directBranches<<","
     <<"\n\"conditionalBranches\":"<<r.conditionalBranches<<","
     <<"\n\"linkBranches\":"<<r.linkBranches<<","
     <<"\n\"indirectBranches\":"<<r.indirectBranches<<","
     <<"\n\"linkTargetsWithinImage\":"<<r.linkTargetsWithinImage<<","
     <<"\n\"linkTargetsOutsideImage\":"<<r.linkTargetsOutsideImage<<","
     <<"\n\"candidateTargetsVerifiedAsFunctions\":false,"
     <<"\n\"partial\":"<<(r.partial?"true":"false")<<","
     <<"\n\"sampleTruncated\":"<<(r.sampleTruncated?"true":"false")<<","
     <<"\n\"candidateTargetSamples\":[";
    for(std::size_t i=0;i<r.candidateTargetSamples.size();++i) {
        if(i)o<<",";
        o<<r.candidateTargetSamples[i];
    }
    o<<"],\n\"blockers\":[";
    for(std::size_t i=0;i<r.blockers.size();++i) {
        if(i)o<<",";
        o<<"\""<<lumavun::quote(r.blockers[i])<<"\"";
    }
    o<<"],\n\"nativeExecutableCreated\":false\n}\n";
    return o.str();
}
} // namespace lumavun::ppu
