#pragma once
#include <cstdint>
#include <string>
#include <vector>
namespace runtime {
struct Box {
    uintptr_t id = 0;
    float headX = 0, headY = 0, feetX = 0, feetY = 0;
    float distance = 0;
    bool projected = false;
    std::string label;
};
struct Frame { std::vector<Box> boxes; unsigned players = 0; };
Frame snapshot();
void setEnabled(bool enabled);
void installGameHooks();
const char* status();
}
