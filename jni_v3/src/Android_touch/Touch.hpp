#pragma once
#include <cstdint>

namespace touch {
    bool init(int32_t _screen_w, int32_t _screen_h, uint8_t _orientation);
    void update(int32_t _screen_w, int32_t _screen_h, uint8_t _orientation);
    void updateOrientation(uint8_t _orientation);
    // Отдать накопленные касания в ImGui. Только из главного потока,
    // до ImGui::NewFrame() (см. drawBegin).
    void pump();
    void shutdown();
}
