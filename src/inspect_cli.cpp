#include "inspector.hpp"
#include <iostream>
int main(int argc,char**argv){
  if(argc!=2){std::cerr<<"Usage: lumavun_inspect <game-file-or-folder>\n";return 2;}
  auto r=lumavun::inspect(std::filesystem::u8path(argv[1]));
  std::cout<<lumavun::toJson(r);
  return r.status=="NOT_FOUND" || r.status=="READ_ERROR" ? 1 : 0;
}