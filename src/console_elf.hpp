#pragma once
// V0.6: Bounded, read-only PS2/PS3 executable feasibility inspection.
// This module does NOT decrypt SELF, translate instructions, or execute game code.
#include "inspector.hpp"
#include <array>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <limits>
#include <sstream>
#include <string>
#include <system_error>
#include <vector>

namespace lumavun::feasibility {
namespace fs = std::filesystem;

struct Segment {
    std::uint64_t offset=0, bytes=0, memory=0, vaddr=0;
    std::uint32_t flags=0;
};
struct Report {
    fs::path requested;
    fs::path inspected;
    std::string platform="Unknown";
    std::string format="Unknown";
    std::string status="UNRECOGNIZED";
    std::string detail="No recognized executable header.";
    std::string architecture="Unknown";
    std::string byteOrder="Unknown";
    std::uint64_t fileBytes=0, entry=0;
    std::vector<Segment> segments;
    std::vector<std::string> blockers;
};
inline std::uint64_t number(const unsigned char* bytes,std::size_t offset,
                            unsigned count,bool little) {
    std::uint64_t value=0;
    if(little) {
        for(unsigned i=0;i<count;++i)
            value|=std::uint64_t(bytes[offset+i])<<(8*i);
    } else {
        for(unsigned i=0;i<count;++i)
            value=(value<<8)|bytes[offset+i];
    }
    return value;
}
inline bool within(std::uint64_t offset,std::uint64_t size,std::uint64_t limit) {
    return offset<=limit && size<=limit-offset;
}
inline fs::path locate(const fs::path& request) {
    std::error_code ec;
    if(!fs::is_directory(request,ec)||ec)return request;
    const fs::path game=fs::is_directory(request/"PS3_GAME",ec)&&!ec
        ? request/"PS3_GAME" : request;
    if(fs::is_regular_file(game/"USRDIR"/"EBOOT.BIN",ec)&&!ec)
        return game/"USRDIR"/"EBOOT.BIN";
    return request;
}
inline Report probe(const fs::path& input) {
    Report out;
    out.requested=input;
    out.inspected=locate(input);
    std::error_code ec;
    if(!fs::is_regular_file(out.inspected,ec)||ec) {
        out.status="SOURCE_UNAVAILABLE";
        out.detail="Expected a regular ELF/SELF file, or a PS3_GAME folder with USRDIR/EBOOT.BIN.";
        out.blockers={"SOURCE_UNAVAILABLE"};
        return out;
    }
    const auto size=fs::file_size(out.inspected,ec);
    if(ec||size>static_cast<std::uint64_t>(std::numeric_limits<std::streamoff>::max())) {
        out.status="READ_ERROR";out.detail="Unable to determine bounded file size.";
        out.blockers={"READ_ERROR"};
        return out;
    }
    out.fileBytes=size;
    std::ifstream file(out.inspected,std::ios::binary);
    if(!file) {
        out.status="READ_ERROR";out.detail="Cannot open file.";
        out.blockers={"READ_ERROR"};
        return out;
    }
    std::array<unsigned char,64> hdr{};
    file.read(reinterpret_cast<char*>(hdr.data()),
              static_cast<std::streamsize>(std::min<std::uint64_t>(size,hdr.size())));
    if(file.bad()) {
        out.status="READ_ERROR";out.detail="Header read failed.";
        out.blockers={"READ_ERROR"};
        return out;
    }
    if(size>=4 && hdr[0]=='S'&&hdr[1]=='C'&&hdr[2]=='E'&&hdr[3]==0) {
        out.platform="PS3/Vita candidate";
        out.format="SCE SELF container";
        out.status="SELF_CONTAINER";
        out.detail="SCE signed container detected. Platform and encryption status not authenticated; no decryption attempted.";
        out.blockers={"SELF_UNPACK_REQUIRED","PLATFORM_UNVERIFIED",
                      "PPU_SPU_RUNTIME_UNAVAILABLE","RSX_AUDIO_INPUT_UNAVAILABLE"};
        return out;
    }
    if(size<52 || hdr[0]!=0x7f || hdr[1]!='E' || hdr[2]!='L'||hdr[3]!='F') {
        out.status="NOT_ELF";
        out.detail="File is not a supported ELF or SCE SELF header.";
        out.blockers={"ELF_OR_SELF_REQUIRED"};
        return out;
    }
    const unsigned cls=hdr[4], endian=hdr[5];
    if((cls!=1&&cls!=2)||(endian!=1&&endian!=2)||hdr[6]!=1) {
        out.status="INVALID_ELF";
        out.detail="Unsupported ELF class, byte order, or version.";
        out.blockers={"INVALID_ELF"};
        return out;
    }
    const bool little=endian==1;
    const std::uint64_t expected=cls==1?52:64;
    if(size<expected) {
        out.status="INVALID_ELF";out.detail="Truncated ELF header.";
        out.blockers={"INVALID_ELF"};
        return out;
    }
    const auto machine=number(hdr.data(),18,2,little);
    const auto type=number(hdr.data(),16,2,little);
    const auto entry=number(hdr.data(),cls==1?24:24,cls==1?4:8,little);
    const auto phoff=number(hdr.data(),cls==1?28:32,cls==1?4:8,little);
    const auto phentsize=number(hdr.data(),cls==1?42:54,2,little);
    const auto phnum=number(hdr.data(),cls==1?44:56,2,little);
    const auto ehsize=number(hdr.data(),cls==1?40:52,2,little);
    out.format=cls==1?"ELF32":"ELF64";
    out.byteOrder=little?"Little endian":"Big endian";
    out.entry=entry;
    if(cls==1&&little&&machine==8) {
        out.platform="PS2 candidate";out.architecture="MIPS R5900 candidate";
        out.blockers={"MIPS_VU_IOP_RUNTIME_UNAVAILABLE","GS_AUDIO_INPUT_UNAVAILABLE"};
    } else if(cls==2&&!little&&machine==21) {
        out.platform="PS3 candidate";out.architecture="PowerPC64 PPU candidate";
        out.blockers={"PPU_RECOMPILER_UNAVAILABLE","SPU_RUNTIME_UNAVAILABLE",
                      "RSX_AUDIO_INPUT_UNAVAILABLE"};
    } else if(cls==1&&!little&&machine==23) {
        out.platform="PS3 SPU candidate";out.architecture="SPU candidate";
        out.blockers={"SPU_RECOMPILER_UNAVAILABLE","PPU_INTEGRATION_UNAVAILABLE"};
    } else {
        out.status="UNSUPPORTED_ARCHITECTURE";
        out.detail="ELF architecture/endian combination is not a supported PS2/PS3 executable candidate.";
        out.blockers={"UNSUPPORTED_ARCHITECTURE"};
        return out;
    }
    if((type!=2&&type!=3)||ehsize!=expected||phnum==0||phnum>512||
       phentsize!=(cls==1?32:56)||
       !within(phoff,phnum*phentsize,size)) {
        out.status="INVALID_ELF";
        out.detail="Invalid ELF type/header size or program header table bounds.";
        out.blockers.insert(out.blockers.begin(),"INVALID_ELF");
        return out;
    }
    std::array<unsigned char,56> ph{};
    std::uint32_t executable=0;
    for(std::uint64_t i=0;i<phnum;++i) {
        const auto where=phoff+i*phentsize;
        file.clear();
        file.seekg(static_cast<std::streamoff>(where));
        file.read(reinterpret_cast<char*>(ph.data()),static_cast<std::streamsize>(phentsize));
        if(!file) {
            out.status="INVALID_ELF";out.detail="Truncated program header.";
            out.blockers.insert(out.blockers.begin(),"INVALID_ELF");return out;
        }
        if(number(ph.data(),0,4,little)!=1)continue; // PT_LOAD
        Segment seg;
        seg.flags=static_cast<std::uint32_t>(number(ph.data(),cls==1?24:4,4,little));
        seg.offset=number(ph.data(),cls==1?4:8,cls==1?4:8,little);
        seg.vaddr=number(ph.data(),cls==1?8:16,cls==1?4:8,little);
        seg.bytes=number(ph.data(),cls==1?16:32,cls==1?4:8,little);
        seg.memory=number(ph.data(),cls==1?20:40,cls==1?4:8,little);
        if(seg.bytes>seg.memory||!within(seg.offset,seg.bytes,size)) {
            out.status="INVALID_ELF";out.detail="PT_LOAD segment is outside file or filesz exceeds memsz.";
            out.blockers.insert(out.blockers.begin(),"INVALID_ELF");out.segments.clear();return out;
        }
        if(seg.flags&1)++executable;
        out.segments.push_back(seg);
    }
    if(out.segments.empty()||executable==0) {
        out.status="INCOMPLETE_ELF";
        out.detail="No executable PT_LOAD segment found; cannot analyze a game entry path.";
        out.blockers.insert(out.blockers.begin(),"EXECUTABLE_SEGMENT_MISSING");
        return out;
    }
    out.status="STRUCTURE_VERIFIED";
    out.detail="ELF header and PT_LOAD segment ranges verified. Game identity, decryption and native port feasibility remain unproven.";
    out.blockers.insert(out.blockers.begin(),"GAME_IDENTITY_UNVERIFIED");
    return out;
}
inline std::string json(const Report& r) {
    std::ostringstream out;
    out<<"{\n\"schema\":\"lumavun.binary-feasibility.v1\","
       <<"\n\"requested\":\""<<lumavun::quote(r.requested.u8string())<<"\","
       <<"\n\"inspected\":\""<<lumavun::quote(r.inspected.u8string())<<"\","
       <<"\n\"platform\":\""<<lumavun::quote(r.platform)<<"\","
       <<"\n\"format\":\""<<lumavun::quote(r.format)<<"\","
       <<"\n\"architecture\":\""<<lumavun::quote(r.architecture)<<"\","
       <<"\n\"endian\":\""<<lumavun::quote(r.byteOrder)<<"\","
       <<"\n\"status\":\""<<lumavun::quote(r.status)<<"\","
       <<"\n\"fileBytes\":"<<r.fileBytes<<","
       <<"\n\"entry\":"<<r.entry<<","
       <<"\n\"segments\":[";
    for(std::size_t i=0;i<r.segments.size();++i) {
        if(i)out<<",";
        const auto& s=r.segments[i];
        out<<"{\"offset\":"<<s.offset<<",\"fileSize\":"<<s.bytes
           <<",\"memorySize\":"<<s.memory<<",\"virtualAddress\":"<<s.vaddr
           <<",\"flags\":"<<s.flags<<"}";
    }
    out<<"],\n\"blockers\":[";
    for(std::size_t i=0;i<r.blockers.size();++i) {
        if(i)out<<",";
        out<<"\""<<lumavun::quote(r.blockers[i])<<"\"";
    }
    out<<"],\n\"detail\":\""<<lumavun::quote(r.detail)<<"\","
       <<"\n\"nativeExecutableCreated\":false\n}\n";
    return out.str();
}
} // namespace lumavun::feasibility
