#pragma once
#include <algorithm>
#include <array>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
namespace lumavun {
namespace fs=std::filesystem;
struct Inspection {
  fs::path path;std::string platform="Unknown",format="Unknown",status="UNRECOGNIZED";
  std::string detail="Unknown format; no native conversion attempted.";
};
inline std::string lower(std::string s){
  std::transform(s.begin(),s.end(),s.begin(),
    [](unsigned char c){return static_cast<char>(std::tolower(c));});return s;
}
inline bool file(const fs::path& p){std::error_code e;return fs::is_regular_file(p,e)&&!e;}
inline bool dir(const fs::path& p){std::error_code e;return fs::is_directory(p,e)&&!e;}
inline Inspection inspect(const fs::path& p) {
  Inspection r;r.path=p;std::error_code ec;
  if(!fs::exists(p,ec)||ec){r.status="NOT_FOUND";r.detail="Path does not exist.";return r;}
  if(dir(p)){
    auto root=dir(p/"PS3_GAME")?p/"PS3_GAME":p;
    if(file(root/"PARAM.SFO")&&file(root/"USRDIR"/"EBOOT.BIN")){
      r.platform="PS3";r.format="PS3_GAME folder";r.status="REQUIRES_NATIVE_PORT";
      r.detail="PS3 game folder recognized; no working native runtime or game recompiler is installed.";return r;
    }
    if(file(root/"SYSTEM.CNF")){
      r.platform="PS2 candidate";r.format="Extracted game folder";r.status="REQUIRES_NATIVE_PORT";
      r.detail="SYSTEM.CNF detected; verify the PS2 game before recompilation.";return r;
    }
    r.format="Folder";r.detail="No supported game folder structure identified.";return r;
  }
  if(!file(p))return r;
  std::array<unsigned char,64> b{};std::ifstream f(p,std::ios::binary);
  if(!f){r.status="READ_ERROR";r.detail="Cannot read file.";return r;}
  f.read(reinterpret_cast<char*>(b.data()),b.size());auto n=f.gcount();
  if(n>=8 && std::string(reinterpret_cast<const char*>(b.data()),8)=="PS-X EXE"){
    r.platform="PS1";r.format="PS-X EXE";r.status="REQUIRES_NATIVE_PORT";
    r.detail="PS1 executable recognized; recompilation backend is not integrated.";return r;
  }
  if(n>=20 && b[0]==127 && b[1]=='E'&&b[2]=='L'&&b[3]=='F'){
    unsigned machine=b[5]==1?(unsigned(b[18])|(unsigned(b[19])<<8)):
      (unsigned(b[19])|(unsigned(b[18])<<8));
    r.format=b[4]==2?"ELF64":"ELF32";
    r.platform=machine==8?"PS2 MIPS ELF candidate":
      (machine==21&&b[4]==2)?"PS3 PPC64 ELF candidate":"Unknown ELF";
    r.status="REQUIRES_NATIVE_PORT";
    r.detail="ELF signature checked; no native port has been built.";return r;
  }
  auto name=lower(p.filename().string()), ext=lower(p.extension().string());
  if(name=="eboot.bin"){r.platform="PS3 candidate";r.format="EBOOT.BIN";
    r.status="NEEDS_ELF";r.detail="Encrypted EBOOT filename alone is not a decrypted ELF.";}
  else if(ext==".cue"){r.platform="PS1 candidate";r.format="CUE";
    r.status="NEEDS_DISC_VERIFICATION";r.detail="Track images and console type not yet verified.";}
  else if(ext==".iso"||ext==".bin"||ext==".img"){r.format="Disc image";
    r.status="NEEDS_DISC_PARSER";r.detail="Disc filesystem parsing not implemented.";}
  else if(ext==".pkg"){r.format="Package";r.status="NEEDS_PACKAGE_PARSER";
    r.detail="Package extraction is not implemented.";}
  return r;
}
inline std::string quote(const std::string& s){
  std::ostringstream out;const char* hex="0123456789abcdef";
  for(unsigned char c:s){
    if(c=='"')out<<"\\\"";else if(c=='\\')out<<"\\\\";
    else if(c=='\n')out<<"\\n";else if(c=='\r')out<<"\\r";
    else if(c=='\t')out<<"\\t";
    else if(c<32)out<<"\\u00"<<hex[c>>4]<<hex[c&15];
    else out<<static_cast<char>(c);
  }return out.str();
}
inline std::string toJson(const Inspection& r){
  return "{\n\"path\":\""+quote(r.path.u8string())+"\",\n\"platform\":\""+quote(r.platform)+
    "\",\n\"format\":\""+quote(r.format)+"\",\n\"status\":\""+quote(r.status)+
    "\",\n\"detail\":\""+quote(r.detail)+"\"\n}\n";
}
}