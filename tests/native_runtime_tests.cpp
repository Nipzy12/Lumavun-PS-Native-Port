#include "native_runtime_state.hpp"
#include <iostream>
#include <stdexcept>
#include <string>
using namespace lumavun::runtime;
static int checks=0;
static void check(bool value,const std::string& name) {
    ++checks;
    if(!value)throw std::runtime_error("native runtime state test: "+name);
}
int main(){
    Snapshot original{4,7,2};
    check(valid(original),"valid score/turn");
    const auto packed=encode(original);
    check(packed.size()==24,"fixed save size");
    check(packed[0]=='S' && packed[1]=='P' && packed[2]=='V' && packed[3]=='L',
          "save magic");
    Snapshot loaded{};
    check(decode(packed,loaded),"valid save decodes");
    check(loaded.position==4 && loaded.turn==7 && loaded.score==2,"round-trip state");
    auto corrupt=packed;
    corrupt[16]^=1;
    loaded={3,3,1};
    check(!decode(corrupt,loaded),"tamper detection");
    check(loaded.position==3 && loaded.turn==3 && loaded.score==1,
          "failed decode preserves destination");
    corrupt=packed;
    put32(corrupt,8,5);
    put32(corrupt,20,checksum(corrupt));
    check(!decode(corrupt,loaded),"reject out of board position even with valid hash");
    corrupt=packed;
    put32(corrupt,4,999);
    put32(corrupt,20,checksum(corrupt));
    check(!decode(corrupt,loaded),"reject unsupported save version");
    check(!valid(Snapshot{0,1,2}),"score cannot exceed turns");
    check(!valid(Snapshot{0,11,2}),"turns bounded");
    const auto first=tileRect(960,540,0);
    const auto last=tileRect(960,540,4);
    check(first.width>0 && first.height>0,"valid viewport geometry");
    check(last.left>first.left && last.left+last.width<=960,"tiles fit screen");
    check(last.top==first.top && last.height==first.height,"tiles consistent");
    check(tileRect(100,100,0).width==0,"reject undersize window");
    check(tileRect(960,540,5).width==0,"reject out-of-bounds cell");
    std::cout<<checks<<" native runtime state checks passed\n";
}
