#pragma once
#include "inspector.hpp"
#include <algorithm>
#include <cctype>
#include <istream>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace lumavun::port {
struct Profile {
    std::string id;
    std::string platform;
    std::string title;
    std::string state;
    std::string notes;
};
struct Backend {
    std::string id;
    std::string platform;
    bool implemented = false;
    std::string requirements;
};
struct Stage {
    std::string id;
    std::string status;
    std::string detail;
};
struct Plan {
    Inspection inspection;
    std::string profileId;
    std::string backendId;
    std::string code = "BLOCKED";
    bool canBuild = false;
    std::vector<Stage> stages;
    std::vector<std::string> blockers;
};

inline std::string platformOf(const std::string& value) {
    for (auto prefix : {"PS1", "PS2", "PS3", "PS4", "PS5"}) {
        if (value.compare(0, 3, prefix) == 0) return prefix;
    }
    return "Unknown";
}
inline std::vector<Backend> backendRegistry() {
    return {
        {"ps1-static", "PS1", false, "Game-specific MIPS recompiler plus PS1 native runtime"},
        {"ps2-static", "PS2", false, "Game-specific MIPS/VU recompiler plus PS2 native runtime"},
        {"ps3-static", "PS3", false, "Game-specific PPU/SPU recompiler plus RSX/audio runtime"}
    };
}
inline bool validId(const std::string& id) {
    if (id.empty() || id.size() > 64) return false;
    for (unsigned char c : id)
        if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-')) return false;
    return true;
}
inline std::vector<Profile> readCatalog(std::istream& in) {
    std::vector<Profile> records;
    std::string line;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty() || line[0] == '#') continue;
        if (line.size() > 1024 || records.size() >= 5000)
            throw std::runtime_error("Compatibility database exceeds limits");
        std::vector<std::string> fields;
        std::size_t from = 0;
        for (std::size_t i = 0; i <= line.size(); ++i) {
            if (i == line.size() || line[i] == '\t') {
                fields.emplace_back(line.substr(from, i - from));
                from = i + 1;
            }
        }
        if (fields.size() != 5 || !validId(fields[0]) ||
            platformOf(fields[1]) != fields[1] || fields[3] != "research-only")
            throw std::runtime_error("Invalid compatibility catalog row");
        for (const auto& previous : records)
            if (previous.id == fields[0])
                throw std::runtime_error("Duplicate compatibility profile ID");
        records.push_back({fields[0], fields[1], fields[2], fields[3], fields[4]});
    }
    if (in.bad()) throw std::runtime_error("Error reading compatibility database");
    return records;
}
inline Plan makePlan(const Inspection& inspection,
                     const std::vector<Profile>& database,
                     const std::string& requestedProfile) {
    Plan plan;
    plan.inspection = inspection;
    plan.profileId = requestedProfile;
    const auto detected = platformOf(inspection.platform);
    const bool exists = inspection.status != "NOT_FOUND" && inspection.status != "READ_ERROR";
    const bool recognized = exists && detected != "Unknown";
    plan.stages.push_back({"source", recognized ? "READY" : "BLOCKED",
        recognized ? "Console family identified, not game identity verified" :
                     "Missing, unreadable, or unrecognized console source"});
    if (!exists) plan.blockers.push_back("SOURCE_UNAVAILABLE");
    else if (!recognized) plan.blockers.push_back("PLATFORM_UNVERIFIED");

    const Profile* chosen = nullptr;
    for (const auto& entry : database)
        if (entry.id == requestedProfile) { chosen = &entry; break; }

    if (requestedProfile.empty()) {
        plan.stages.push_back({"compatibility-profile", "BLOCKED", "No game-specific profile selected"});
        plan.blockers.push_back("PROFILE_REQUIRED");
    } else if (!chosen) {
        plan.stages.push_back({"compatibility-profile", "BLOCKED", "Profile ID not in compatibility database"});
        plan.blockers.push_back("PROFILE_NOT_FOUND");
    } else if (recognized && chosen->platform != detected) {
        plan.stages.push_back({"compatibility-profile", "BLOCKED", "Selected profile console does not match inspected source"});
        plan.blockers.push_back("PLATFORM_MISMATCH");
    } else {
        plan.stages.push_back({"compatibility-profile", "BLOCKED",
            "Profile is research-only; source hash and game identity are not authenticated"});
        plan.blockers.push_back("GAME_IDENTITY_UNVERIFIED");
    }

    bool backendRecognized = false;
    for (const auto& backend : backendRegistry()) {
        if (backend.platform == detected) {
            plan.backendId = backend.id;
            backendRecognized = true;
            if (!backend.implemented) plan.blockers.push_back("RECOMPILER_NOT_IMPLEMENTED");
            break;
        }
    }
    if (!backendRecognized) plan.blockers.push_back("NO_BACKEND_FOR_PLATFORM");
    plan.stages.push_back({"recompiler", "BLOCKED",
        backendRecognized ? "Adapter interface defined, implementation not connected" :
                            "No native recompilation adapter for detected platform"});
    plan.stages.push_back({"native-runtime", "BLOCKED",
        "Per-game graphics, audio, memory, input and system API runtime not implemented"});
    plan.stages.push_back({"compile", "BLOCKED", "Native translation and validated runtime required"});
    plan.stages.push_back({"verification", "BLOCKED",
        "Gameplay, input, sound, saves, and performance must be tested before release"});
    plan.blockers.push_back("NATIVE_RUNTIME_UNAVAILABLE");
    plan.code = !exists ? "INVALID_SOURCE" : "UNSUPPORTED_GAME";
    plan.canBuild = false;
    return plan;
}
inline std::string planJson(const Plan& p) {
    std::ostringstream o;
    o << "{\n\"schema\":\"lumavun.native-port-plan.v1\","
      << "\n\"result\":\"" << quote(p.code) << "\","
      << "\n\"canBuild\":" << (p.canBuild ? "true" : "false") << ","
      << "\n\"input\":" << toJson(p.inspection).substr(0, toJson(p.inspection).size()-1) << ","
      << "\n\"profile\":\"" << quote(p.profileId) << "\","
      << "\n\"backend\":\"" << quote(p.backendId) << "\",\n\"stages\":[";
    for (std::size_t i=0;i<p.stages.size();++i) {
        const auto& s=p.stages[i];
        if (i) o << ",";
        o << "{\"id\":\"" << quote(s.id) << "\",\"status\":\"" << quote(s.status)
          << "\",\"detail\":\"" << quote(s.detail) << "\"}";
    }
    o << "],\n\"blockers\":[";
    for (std::size_t i=0;i<p.blockers.size();++i) {
        if (i) o << ",";
        o << "\"" << quote(p.blockers[i]) << "\"";
    }
    o << "],\n\"nativeExecutableCreated\":false\n}\n";
    return o.str();
}
// Contract for future game-specific recompilers. No emulator wrapper or fake success backend.
struct BuildResult {
    bool success = false;
    std::string code = "RECOMPILER_NOT_IMPLEMENTED";
    std::string detail = "No registered native code translator exists for this game";
};
class IRecompilerBackend {
public:
    virtual ~IRecompilerBackend() = default;
    virtual Backend describe() const = 0;
    virtual BuildResult compile(const Inspection& input, const Profile& profile) = 0;
};
} // namespace lumavun::port