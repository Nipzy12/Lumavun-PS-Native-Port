#include "port_pipeline.hpp"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

namespace fs=std::filesystem;
using namespace lumavun;
using namespace lumavun::port;

static int usage() {
    std::cerr << "LUMAVUN Native Port Pipeline V0.4\n"
              << "Commands:\n"
              << "  lumavun_port backends\n"
              << "  lumavun_port compatibility [--catalog path]\n"
              << "  lumavun_port plan  <game-file-or-folder> [--profile id] [--catalog path] [--report path]\n"
              << "  lumavun_port build <game-file-or-folder> [--profile id] [--catalog path] [--report path]\n"
              << "NOTE: Build intentionally fails until a compatible genuine native backend is implemented.\n";
    return 2;
}
static fs::path defaultCatalog(const char* argv0) {
    auto local = fs::u8path(argv0).parent_path() / "data" / "compatibility.tsv";
    if (fs::exists(local)) return local;
    return fs::path("data") / "compatibility.tsv";
}
static std::string backendsJson() {
    std::string result = "{\"backends\":[";
    for (const auto& b : backendRegistry()) {
        if (result.back() != '[') result += ",";
        result += "{\"id\":\""+quote(b.id)+"\",\"platform\":\""+quote(b.platform)+
            "\",\"implemented\":false,\"requirements\":\""+quote(b.requirements)+"\"}";
    }
    return result + "],\"nativePortingAvailable\":false}\n";
}
int main(int argc, char** argv) {
    if (argc < 2) return usage();
    const std::string command = argv[1];
    if (command == "backends") {
        if (argc != 2) return usage();
        std::cout << backendsJson();
        return 0;
    }
    if (command != "plan" && command != "build" && command != "compatibility")
        return usage();
    if (command != "compatibility" && argc < 3) return usage();
    std::string profileId;
    fs::path catalog = defaultCatalog(argv[0]);
    fs::path report;
    for (int i=command=="compatibility"?2:3; i<argc; ++i) {
        std::string option=argv[i];
        if (i+1>=argc) return usage();
        if (option=="--profile" && command!="compatibility")profileId=argv[++i];
        else if (option=="--catalog")catalog=fs::u8path(argv[++i]);
        else if (option=="--report" && command!="compatibility")report=fs::u8path(argv[++i]);
        else return usage();
    }
    try {
        std::ifstream file(catalog,std::ios::binary);
        if (!file) {
            std::cerr << "CATALOG_NOT_FOUND: " << catalog.u8string() << "\n";
            return 5;
        }
        auto records=readCatalog(file);
        if (command=="compatibility") {
            std::cout << "{\"profiles\":[";
            for (std::size_t i=0;i<records.size();++i) {
                const auto& p=records[i];
                if (i)std::cout << ",";
                std::cout << "{\"id\":\"" << quote(p.id) << "\",\"platform\":\"" <<
                    quote(p.platform) << "\",\"title\":\"" << quote(p.title) <<
                    "\",\"state\":\"" << quote(p.state) << "\"}";
            }
            std::cout << "],\"nativeGamePorts\":0}\n";
            return 0;
        }
        const auto input=fs::u8path(argv[2]);
        auto inspection=inspect(input);
        auto plan=makePlan(inspection,records,profileId);
        auto json=planJson(plan);
        if (!report.empty()) {
            std::error_code ec;
            if (fs::exists(input,ec) && fs::equivalent(input,report,ec) && !ec) {
                std::cerr << "INVALID_REPORT_PATH: refuses to overwrite the source game file\n";
                return 6;
            }
            fs::create_directories(report.parent_path().empty()?fs::current_path():report.parent_path());
            std::ofstream out(report,std::ios::binary|std::ios::trunc);
            if (!out) {
                std::cerr << "REPORT_WRITE_FAILED\n";
                return 6;
            }
            out << json;
            out.flush();
            if (!out.good())return 6;
        }
        std::cout << json;
        if (command=="build") {
            std::cerr << "BUILD_BLOCKED: no executable created. Native backend/runtime unavailable.\n";
            return 4;
        }
        return plan.code=="INVALID_SOURCE"?2:0;
    } catch(const std::exception& error) {
        std::cerr << "PIPELINE_ERROR: " << error.what() << "\n";
        return 5;
    }
}