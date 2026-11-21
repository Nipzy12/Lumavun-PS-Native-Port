#include "inspector.hpp"
#include <chrono>
#include <fstream>
#include <iostream>
#include <stdexcept>
void chk(bool ok){if(!ok)throw std::runtime_error("inspection test failed");}
int main(){
  namespace fs=std::filesystem;
  auto p=fs::temp_directory_path()/("lv-check-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
  fs::create_directories(p/"ps3"/"PS3_GAME"/"USRDIR");
  std::ofstream(p/"ps3"/"PS3_GAME"/"PARAM.SFO").put('a');
  std::ofstream(p/"ps3"/"PS3_GAME"/"USRDIR"/"EBOOT.BIN").put('b');
  chk(lumavun::inspect(p/"ps3").platform=="PS3");
  fs::create_directories(p/"ps2");std::ofstream(p/"ps2"/"SYSTEM.CNF").put('x');
  chk(lumavun::inspect(p/"ps2").platform=="PS2 candidate");
  {std::ofstream f(p/"psx.bin",std::ios::binary);f.write("PS-X EXE",8);}
  chk(lumavun::inspect(p/"psx.bin").platform=="PS1");
  {std::ofstream f(p/"ps3.elf",std::ios::binary);
    char b[64]={};b[0]=127;b[1]='E';b[2]='L';b[3]='F';b[4]=2;b[5]=2;b[19]=21;f.write(b,64);}
  chk(lumavun::inspect(p/"ps3.elf").platform=="PS3 PPC64 ELF candidate");
  std::ofstream(p/"disc.iso").put('a');
  chk(lumavun::inspect(p/"disc.iso").status=="NEEDS_DISC_PARSER");
  std::ofstream(p/"EBOOT.BIN").put('a');
  chk(lumavun::inspect(p/"EBOOT.BIN").status=="NEEDS_ELF");
  chk(lumavun::inspect(p/"not_here").status=="NOT_FOUND");
  chk(lumavun::quote("a\"b")=="a\\\"b");
  fs::remove_all(p);std::cout<<"8 inspection checks passed\n";
}