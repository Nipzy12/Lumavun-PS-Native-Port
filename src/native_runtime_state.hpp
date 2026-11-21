#pragma once
// Minimal host-side runtime helpers for the ORIGINAL PS1 homebrew demo only.
// Not a PlayStation BIOS, GS/RSX renderer, SPU runtime, or universal save format.
#include <array>
#include <algorithm>
#include <cstddef>
#include <cstdint>

namespace lumavun::runtime {
struct Snapshot {
    std::uint32_t position=0;
    std::uint32_t turn=0;
    std::uint32_t score=0;
};
struct Rect {
    int left=0;
    int top=0;
    int width=0;
    int height=0;
};
using SaveBytes=std::array<std::uint8_t,24>;

inline bool valid(const Snapshot& s) {
    return s.position<=4 && s.turn<=10 && s.score<=10 && s.score<=s.turn;
}
inline void put32(SaveBytes& b,std::size_t at,std::uint32_t number) {
    for(unsigned i=0;i<4;++i)b[at+i]=static_cast<std::uint8_t>(number>>(i*8));
}
inline std::uint32_t get32(const SaveBytes& b,std::size_t at) {
    return std::uint32_t(b[at])|
           (std::uint32_t(b[at+1])<<8)|
           (std::uint32_t(b[at+2])<<16)|
           (std::uint32_t(b[at+3])<<24);
}
inline std::uint32_t checksum(const SaveBytes& b) {
    std::uint32_t hash=2166136261u;
    for(std::size_t i=0;i<20;++i) {
        hash^=b[i];
        hash*=16777619u;
    }
    return hash;
}
inline SaveBytes encode(const Snapshot& snapshot) {
    SaveBytes b{};
    put32(b,0,0x4c565053u); // "SPVL" in little-endian bytes: demo magic
    put32(b,4,1u);
    put32(b,8,snapshot.position);
    put32(b,12,snapshot.turn);
    put32(b,16,snapshot.score);
    put32(b,20,checksum(b));
    return b;
}
inline bool decode(const SaveBytes& b,Snapshot& output) {
    if(get32(b,0)!=0x4c565053u || get32(b,4)!=1u ||
       get32(b,20)!=checksum(b))return false;
    const Snapshot candidate{get32(b,8),get32(b,12),get32(b,16)};
    if(!valid(candidate))return false;
    output=candidate;
    return true;
}
// Lightweight five-cell geometry for the native Win32 GDI demo.
inline Rect tileRect(int width,int height,unsigned index) {
    if(width<200 || height<150 || index>=5)return {};
    const int margin=std::max(16,width/20);
    const int gap=std::max(4,width/100);
    const int space=width-margin*2-gap*4;
    const int cell=std::max(1,space/5);
    const int tileH=std::min(170,std::max(48,height/3));
    return {margin+static_cast<int>(index)*(cell+gap),
            std::max(55,height/2-tileH/2),cell,tileH};
}
} // namespace lumavun::runtime
