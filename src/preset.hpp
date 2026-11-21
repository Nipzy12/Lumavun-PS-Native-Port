#pragma once
#include <algorithm>
#include <cctype>
#include <string>
namespace lumavun {
struct Specs {
  std::string cpu, gpu;
  int ramMB=0;
  int dedicatedVideoMB=0;
  int cores=0;
};
struct Preset {
  std::string name;
  int width, height, renderScale, fps;
};
inline std::string lower(std::string v) {
  std::transform(v.begin(),v.end(),v.begin(),
    [](unsigned char c){return static_cast<char>(std::tolower(c));});
  return v;
}
inline Preset recommend(const Specs& s) {
  auto cpu=lower(s.cpu),gpu=lower(s.gpu);
  if((s.ramMB>0 && s.ramMB<=4096) ||
      cpu.find("amd a4")!=std::string::npos ||
      gpu.find("radeon r3")!=std::string::npos)
    return {"Ultra Low",960,540,70,30};
  if((s.ramMB>0 && s.ramMB<8192) || (s.cores>0 && s.cores<=2))
    return {"Low",1280,720,75,30};
  if(s.ramMB>=16384 && s.dedicatedVideoMB>=4096)
    return {"High",1920,1080,100,60};
  return {"Balanced",1280,720,100,60};
}
}