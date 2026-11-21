#include "port_pipeline.hpp"
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
using namespace lumavun;
using namespace lumavun::port;
static int checks=0;
static void check(bool ok,const std::string& label){
    ++checks;
    if (!ok)throw std::runtime_error("FAILED: "+label);
}
template<typename F>static void expectThrows(F fn,const std::string& label){
    bool thrown=false;
    try{fn();}catch(const std::runtime_error&){thrown=true;}
    check(thrown,label);
}
int main(){
    std::istringstream catalog(
        "# Demo compatible database schema\n"
        "gundam-reborn\tPS3\tDynasty Warriors: Gundam Reborn\tresearch-only\tNeeds actual native port\n");
    const auto entries=readCatalog(catalog);
    check(entries.size()==1,"catalog row count");
    check(entries[0].id=="gundam-reborn","catalog id");
    check(backendRegistry().size()==3,"registered backend descriptions");
    for (const auto& backend:backendRegistry())
        check(!backend.implemented,"all native ports remain unavailable");
    expectThrows([]{
        std::istringstream invalid("x/../bad\tPS3\tbad\tresearch-only\tx\n");
        readCatalog(invalid);
    },"reject invalid ID");
    expectThrows([]{
        std::istringstream invalid(
          "x\tPS3\tone\tresearch-only\tgood\n"
          "x\tPS3\ttwo\tresearch-only\tduplicate\n");
        readCatalog(invalid);
    },"reject duplicate ID");
    expectThrows([]{
        std::istringstream invalid("x\tPS3\tone\tplayable\tfalse claim\n");
        readCatalog(invalid);
    },"reject unsupported playable state");
    Inspection ps3;
    ps3.path="gundam/PS3_GAME";
    ps3.platform="PS3";
    ps3.format="PS3_GAME folder";
    ps3.status="REQUIRES_NATIVE_PORT";
    const auto plan=makePlan(ps3,entries,"gundam-reborn");
    check(plan.code=="UNSUPPORTED_GAME","no fake build result");
    check(!plan.canBuild,"build blocked");
    check(plan.backendId=="ps3-static","correct backend selected");
    check(plan.stages.size()==6,"all pipeline stages represented");
    check(plan.stages[0].status=="READY","platform recognition gate");
    check(plan.stages[2].status=="BLOCKED","no recompiler");
    const auto json=planJson(plan);
    check(json.find("\"canBuild\":false")!=std::string::npos,"build false in report");
    check(json.find("\"nativeExecutableCreated\":false")!=std::string::npos,"no fake exe");
    check(json.find("RECOMPILER_NOT_IMPLEMENTED")!=std::string::npos,"explicit blocker");
    check(json.find("gundam-reborn")!=std::string::npos,"profile in report");
    Inspection ps2=ps3;ps2.platform="PS2 candidate";
    const auto mismatch=makePlan(ps2,entries,"gundam-reborn");
    bool hasMismatch=false;
    for (const auto& b:mismatch.blockers)if(b=="PLATFORM_MISMATCH")hasMismatch=true;
    check(hasMismatch,"profile platform mismatch");
    const auto unlisted=makePlan(ps3,entries,"unknown-game");
    check(unlisted.blockers[0]=="PROFILE_NOT_FOUND","unlisted title blocked");
    const auto noProfile=makePlan(ps3,entries,"");
    check(noProfile.blockers[0]=="PROFILE_REQUIRED","title profile required");
    Inspection invalid=ps3;invalid.status="NOT_FOUND";
    const auto missing=makePlan(invalid,entries,"gundam-reborn");
    check(missing.code=="INVALID_SOURCE","missing file rejected");
    Inspection ps5=ps3;ps5.platform="PS5 candidate";
    const auto future=makePlan(ps5,entries,"");
    bool unsupported=false;
    for (const auto& b:future.blockers)if(b=="NO_BACKEND_FOR_PLATFORM")unsupported=true;
    check(unsupported,"PS5 backend not pretended");
    std::cout<<checks<<" V0.4 pipeline checks passed\n";
}