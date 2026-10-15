#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include <memory>
#include "ModuleMath.h"
#include "Vars.h"
namespace runtime {
struct Box {
    uintptr_t id = 0;
    float headX = 0, headY = 0, feetX = 0, feetY = 0;
    float distance = 0;
    float health = 0, maxHealth = 0;
    bool healthKnown = false;
    bool projected = false;
    bool onScreen = false;
    float depth = 0;
    std::string label;
};
struct Frame {
    std::vector<Box> boxes;
    unsigned players = 0, healthUnknown = 0;
    uint64_t redirectedRays = 0;
    bool freecam = false, localHealthKnown = false, localAlive = false;
    bool visibleReady = false, silentHookReady = false;
    modules::Vec3 cameraPosition;
};
void submit(const AllVars& options, modules::Controls controls);
Frame snapshot();
std::shared_ptr<const Frame> sharedSnapshot();
void setEnabled(bool enabled);
void installGameHooks();
const char* status();
}
