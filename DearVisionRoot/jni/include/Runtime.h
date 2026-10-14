#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include "ModuleMath.h"
#include "Vars.h"
namespace runtime {
struct Box {
    uintptr_t id = 0;
    float headX = 0, headY = 0, feetX = 0, feetY = 0;
    float distance = 0;
    bool projected = false;
    bool onScreen = false;
    float depth = 0;
    std::string label;
};
struct Frame { std::vector<Box> boxes; unsigned players = 0; bool freecam = false; modules::Vec3 cameraPosition; };
void submit(const AllVars& options, modules::Controls controls);
Frame snapshot();
void setEnabled(bool enabled);
enum class HookStage { MenuOnly, Connecting, Ready, Failed };
HookStage hookStage();
// Explicit one-shot request. Never invoked by submit(), profiles or module toggles.
bool requestGameHooks();
const char* status();
}
