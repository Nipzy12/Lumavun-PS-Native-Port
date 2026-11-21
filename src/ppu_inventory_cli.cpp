#include "ppu_code_inventory.hpp"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
namespace fs=std::filesystem;

int main(int argc,char**argv) {
    if(argc!=2&&argc!=4) {
        std::cerr<<"Usage: lumavun_ppu_scan <PS3-PPU-ELF-or-PS3_GAME-folder>"
                    " [--report <json-file>]\n"
                    "Scans at most 16 MiB of executable data; never decrypts or recompiles game code.\n";
        return 2;
    }
    if(argc==4&&std::string(argv[2])!="--report")return 2;
    try {
        const fs::path input=fs::u8path(argv[1]);
        const auto result=lumavun::ppu::scan(input);
        const auto report=lumavun::ppu::json(result);
        if(argc==4) {
            const auto output=fs::u8path(argv[3]);
            std::error_code ec;
            if(fs::is_regular_file(result.elf.inspected,ec)&&!ec &&
               fs::exists(output,ec)&&!ec &&
               fs::equivalent(result.elf.inspected,output,ec)&&!ec) {
                std::cerr<<"REPORT_PATH_CONFLICT: refusing to overwrite source executable\n";
                return 6;
            }
            const auto parent=output.parent_path();
            if(!parent.empty())fs::create_directories(parent);
            std::ofstream f(output,std::ios::binary|std::ios::trunc);
            if(!f)return 6;
            f<<report;f.flush();
            if(!f.good())return 6;
        }
        std::cout<<report;
        if(result.status=="SCAN_COMPLETE")return 0;
        if(result.status=="PARTIAL_SCAN")return 3;
        return 4;
    }catch(const std::exception& e) {
        std::cerr<<"PPU_SCAN_ERROR: "<<e.what()<<"\n";
        return 5;
    }
}
