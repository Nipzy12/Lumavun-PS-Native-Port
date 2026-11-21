#include "disc_image.hpp"
#include <algorithm>
#include <stdexcept>
#include <chrono>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <vector>
static void check(bool ok){if(!ok)throw std::runtime_error("test assertion failed");}
namespace fs=std::filesystem;
static void put32(std::vector<unsigned char>& b,std::size_t p,std::uint32_t x){
  for(int i=0;i<4;++i)b[p+i]=static_cast<unsigned char>(x>>(8*i));
}
static void record(std::vector<unsigned char>& b,std::size_t p,
                   const std::string& name,std::uint32_t lba,std::uint32_t length,bool folder=false){
  const std::size_t n=33+name.size()+(name.size()%2==0);
  check(n<=255);
  b[p]=static_cast<unsigned char>(n);put32(b,p+2,lba);put32(b,p+10,length);
  b[p+25]=folder?2:0;b[p+32]=static_cast<unsigned char>(name.size());
  std::copy(name.begin(),name.end(),b.begin()+p+33);
}
static void fixture(const fs::path& p,const std::string& mode,bool raw){
  const unsigned stride=raw?2352:2048,offset=raw?16:0;
  std::vector<unsigned char> bytes(stride*23,0);
  const std::size_t pv=16*stride+offset,dir=20*stride+offset,sys=21*stride+offset;
  bytes[pv]=1;std::copy_n("CD001",5,bytes.begin()+pv+1);bytes[pv+6]=1;
  std::vector<unsigned char> root(2048,0);
  record(root,0,std::string(1,'\0'),20,2048,true);
  std::copy_n(root.begin(),34,bytes.begin()+pv+156);
  std::vector<unsigned char> recs(2048,0);
  if(mode=="PS3")record(recs,0,"PS3_GAME",21,2048,true);
  else record(recs,0,"SYSTEM.CNF;1",21,64);
  std::copy(recs.begin(),recs.end(),bytes.begin()+dir);
  const std::string cnf=mode=="PS2"?"BOOT2 = cdrom0:\\SLUS_999.99;1":"BOOT = cdrom:\\SLUS_999.99;1";
  std::copy(cnf.begin(),cnf.end(),bytes.begin()+sys);
  std::ofstream out(p,std::ios::binary);out.write(reinterpret_cast<char*>(bytes.data()),bytes.size());
}
int main(){
  const auto dir=fs::temp_directory_path()/("lv-disc-"+std::to_string(
    std::chrono::steady_clock::now().time_since_epoch().count()));
  fs::create_directories(dir);
  fixture(dir/"ps1.iso","PS1",false);
  fixture(dir/"ps2.iso","PS2",false);
  fixture(dir/"ps3.iso","PS3",false);
  fixture(dir/"raw.bin","PS2",true);
  check(lumavun::scanDisc(dir/"ps1.iso").platform=="PS1 candidate");
  check(lumavun::scanDisc(dir/"ps2.iso").platform=="PS2 candidate");
  check(lumavun::scanDisc(dir/"ps3.iso").platform=="PS3 candidate");
  check(lumavun::scanDisc(dir/"raw.bin").platform=="PS2 candidate");
  std::ofstream(dir/"truncated.iso").put('x');
  check(!lumavun::scanDisc(dir/"truncated.iso").iso9660);
  check(!lumavun::scanDisc(dir/"missing.iso").iso9660);
  fs::remove_all(dir);
  std::cout<<"6 ISO checks passed\n";
}