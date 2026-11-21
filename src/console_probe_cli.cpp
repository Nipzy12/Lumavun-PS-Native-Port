#include "console_elf.hpp"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
namespace fs=std::filesystem;
int main(int argc,char**argv) {
    if(argc!=2 && argc!=4) {
        std::cerr<<"Usage: lumavun_probe <ELF|SELF|PS3_GAME-folder> [--report <json-file>]\n";
        return 2;
    }
    fs::path output;
    if(argc==4) {
        if(std::string(argv[2])!="--report")return 2;
        output=fs::u8path(argv[3]);
    }
    try {
        const fs::path input=fs::u8path(argv[1]);
        const auto report=lumavun::feasibility::probe(input);
        const auto result=lumavun::feasibility::json(report);
        if(!output.empty()) {
            std::error_code ec;
            if(fs::exists(output,ec) && !ec &&
               fs::is_regular_file(report.inspected,ec) && !ec &&
               fs::equivalent(output,report.inspected,ec) && !ec) {
                std::cerr<<"REPORT_PATH_CONFLICT: refusing to overwrite executable\n";
                return 6;
            }
            const auto parent=output.parent_path();
            if(!parent.empty())fs::create_directories(parent);
            std::ofstream out(output,std::ios::binary|std::ios::trunc);
            if(!out) {
                std::cerr<<"REPORT_WRITE_FAILED\n";return 6;
            }
            out<<result;out.flush();
            if(!out.good())return 6;
        }
        std::cout<<result;
        if(report.status=="STRUCTURE_VERIFIED")return 0;
        if(report.status=="SELF_CONTAINER")return 3;
        if(report.status=="SOURCE_UNAVAILABLE"||report.status=="READ_ERROR")return 2;
        return 4;
    }catch(const std::exception& e){
        std::cerr<<"PROBE_ERROR: "<<e.what()<<"\n";
        return 5;
    }
}