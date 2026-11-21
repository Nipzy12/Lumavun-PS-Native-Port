#pragma once
#include <array>
#include <algorithm>
#include <cctype>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>
namespace lumavun {
struct DiscScan {
  bool iso9660=false;
  std::string format="Unknown disc image";
  std::string platform="Unknown";
  std::string detail="No ISO9660 primary volume descriptor found.";
};
// Read a bounded ISO9660 root directory. No game code is executed or extracted.
inline DiscScan scanDisc(const std::filesystem::path& path) {
  DiscScan out;
  std::ifstream file(path,std::ios::binary|std::ios::ate);
  if(!file){out.detail="Cannot open disc image.";return out;}
  auto end=file.tellg();
  if(end<0)return out;
  const std::uint64_t bytes=static_cast<std::uint64_t>(end);
  for(const std::uint32_t stride:{2048u,2352u}){
    const std::uint32_t offset=stride==2352?16u:0u;
    const std::uint64_t pvdPos=16ull*stride+offset;
    if(pvdPos+2048>bytes)continue;
    std::array<unsigned char,2048> pvd{};
    file.clear();file.seekg(static_cast<std::streamoff>(pvdPos));
    file.read(reinterpret_cast<char*>(pvd.data()),pvd.size());
    if(!file||pvd[0]!=1||std::string(reinterpret_cast<char*>(pvd.data()+1),5)!="CD001"||pvd[6]!=1)continue;
    out.iso9660=true;
    out.format=stride==2352?"Raw Mode1 2352 ISO9660":"ISO9660 2048";
    out.detail="ISO9660 volume detected; platform not confirmed.";
    auto u32=[](const unsigned char* p)->std::uint32_t {
      return std::uint32_t(p[0])|(std::uint32_t(p[1])<<8)|
             (std::uint32_t(p[2])<<16)|(std::uint32_t(p[3])<<24);
    };
    const unsigned char* root=pvd.data()+156;
    if(root[0]<34)return out;
    const std::uint64_t lba=u32(root+2);
    const std::uint32_t length=u32(root+10);
    // Maximum 1 MiB of metadata. Never load full ISO into RAM.
    if(length==0||length>1024*1024)return out;
    const std::uint64_t sectors=(std::uint64_t(length)+2047)/2048;
    if(lba>bytes/stride||sectors>bytes/stride-lba)return out;
    std::vector<unsigned char> records;records.reserve(length);
    for(std::uint64_t i=0;i<sectors;++i){
      std::uint64_t position=(lba+i)*stride+offset;
      if(position+2048>bytes)return out;
      std::array<unsigned char,2048> sector{};
      file.clear();file.seekg(static_cast<std::streamoff>(position));
      file.read(reinterpret_cast<char*>(sector.data()),sector.size());
      if(!file)return out;
      auto count=std::min<std::size_t>(2048,length-records.size());
      records.insert(records.end(),sector.begin(),sector.begin()+count);
    }
    std::uint64_t cnfLba=0;std::uint32_t cnfLength=0;
    bool ps3Directory=false;
    for(std::size_t pos=0;pos<records.size();){
      if(records[pos]==0){pos=(pos/2048+1)*2048;continue;}
      std::size_t len=records[pos];
      if(len<34||pos+len>records.size())break;
      const unsigned char* record=records.data()+pos;
      const std::size_t nameLen=record[32];
      if(33+nameLen>len)break;
      std::string name(reinterpret_cast<const char*>(record+33),nameLen);
      std::transform(name.begin(),name.end(),name.begin(),
        [](unsigned char c){return static_cast<char>(std::toupper(c));});
      if(name=="PS3_GAME")ps3Directory=true;
      if(name=="SYSTEM.CNF;1"||name=="SYSTEM.CNF"){
        cnfLba=u32(record+2);cnfLength=u32(record+10);
      }
      pos+=len;
    }
    if(ps3Directory){
      out.platform="PS3 candidate";
      out.detail="PS3_GAME directory identified. No native port produced.";
      return out;
    }
    if(cnfLength>0&&cnfLength<=4096&&cnfLba<bytes/stride){
      const std::uint64_t position=cnfLba*stride+offset;
      const std::size_t count=std::min<std::uint32_t>(cnfLength,2048u);
      if(position+count<=bytes){
        std::string data(count,'\0');
        file.clear();file.seekg(static_cast<std::streamoff>(position));
        file.read(data.data(),static_cast<std::streamsize>(count));
        if(file){
          std::transform(data.begin(),data.end(),data.begin(),
            [](unsigned char c){return static_cast<char>(std::toupper(c));});
          if(data.find("BOOT2")!=std::string::npos){
            out.platform="PS2 candidate";
            out.detail="PS2-style BOOT2 found in SYSTEM.CNF; no native port produced.";
          }else if(data.find("BOOT")!=std::string::npos){
            out.platform="PS1 candidate";
            out.detail="PS1-style BOOT found in SYSTEM.CNF; no native port produced.";
          }
        }
      }
    }
    return out;
  }
  return out;
}
} // namespace lumavun