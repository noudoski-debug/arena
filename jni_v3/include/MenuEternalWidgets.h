#pragma once

#include "ImGui/imgui.h"
#include <cstddef>

// Complete widget layer adapted from menu.rar's Core/Menu/Widgets.cpp.
// The external build keeps the same custom panels, sliders, combos, inputs
// and color pickers while binding their values to the cheat's Vars.
namespace MenuEternal::Widgets
{
    bool Panel(ImDrawList* drawList, const char* id, ImVec2 pos, ImVec2 size,
               const char* title, bool* expanded);
    void Checkbox(ImDrawList* drawList, const char* id, ImVec2 pos,
                  const char* label, bool* value);
    void SliderFloat(ImDrawList* drawList, const char* id, ImVec2 pos,
                     float width, const char* label, float* value, float min,
                     float max, const char* format, float* visual);
    void SliderInt(ImDrawList* drawList, const char* id, ImVec2 pos,
                   float width, const char* label, int* value, int min, int max,
                   const char* format, float* visual);
    void Combo(ImDrawList* drawList, const char* id, ImVec2 pos, float rowWidth,
               const char* label, int* value, const char* const items[], int count);
    void Input(ImDrawList* drawList, const char* id, ImVec2 pos, float width,
               const char* label, char* buffer, std::size_t bufferSize);
    void ColorPicker(ImDrawList* drawList, const char* id, ImVec2 pos,
                     float rowWidth, const char* label, float color[4]);
}
