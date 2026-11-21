#include "preset.hpp"
#include <stdexcept>
#include <iostream>
static void check(bool ok){if(!ok)throw std::runtime_error("preset test assertion failed");}
int main() {
  using lumavun::Specs;using lumavun::recommend;
  auto a=recommend(Specs{"AMD A4","Radeon R3",4096,0,2});
  check(a.name=="Ultra Low" && a.fps==30 && a.renderScale==70);
  check(recommend(Specs{"Intel Core","Intel HD",6144,0,4}).name=="Low");
  check(recommend(Specs{"Ryzen","Radeon",8192,512,8}).name=="Balanced");
  check(recommend(Specs{"Ryzen","RTX",16384,8192,8}).name=="High");
  check(recommend(Specs{}).name=="Balanced");
  std::cout<<"5 scenarios passed\n";
}