#include "preset.hpp"
#include <cassert>
#include <iostream>
int main() {
  using lumavun::Specs;using lumavun::recommend;
  auto a=recommend(Specs{"AMD A4","Radeon R3",4096,0,2});
  assert(a.name=="Ultra Low" && a.fps==30 && a.renderScale==70);
  assert(recommend(Specs{"Intel Core","Intel HD",6144,0,4}).name=="Low");
  assert(recommend(Specs{"Ryzen","Radeon",8192,512,8}).name=="Balanced");
  assert(recommend(Specs{"Ryzen","RTX",16384,8192,8}).name=="High");
  assert(recommend(Specs{}).name=="Balanced");
  std::cout<<"5 scenarios passed\n";
}