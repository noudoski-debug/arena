#pragma once

#include "ImGui/imgui.h"

// Adapted from menu.rar's Core/Menu/Theme.* for the Android/OpenGL
// external overlay.  The original Win32/DX11 window and tab strip are not
// used here; this theme is applied to the existing responsive EncoreDLC UI.
namespace MenuEternal::Theme
{
    float Clamp(float value, float min, float max);
    ImU32 Accent(int alpha = 255);
    ImU32 AccentSoft(int alpha = 255);
    ImU32 Base(int alpha = 255);
    ImU32 Surface(int alpha = 255);
    ImU32 SurfaceLight(int alpha = 255);
    ImU32 Control(int alpha = 255);
    ImU32 Outline(int alpha = 255);
    ImU32 Text(int alpha = 255);
    ImU32 MutedText(int alpha = 255);
    ImVec4 BaseVec(float alpha = 1.0f);
    ImVec4 SurfaceVec(float alpha = 1.0f);
    ImVec4 ControlVec(float alpha = 1.0f);
    ImVec4 AccentVec(float alpha = 1.0f);
    void ApplyStyle();
}
