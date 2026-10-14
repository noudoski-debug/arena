#include "MenuEternalTheme.h"

namespace MenuEternal::Theme
{
    float Clamp(float value, float min, float max)
    {
        if (value < min) return min;
        if (value > max) return max;
        return value;
    }

    // Apple-inspired graphite palette with system-blue accent.
    ImU32 Accent(int alpha)
    {
        return IM_COL32(10, 132, 255, alpha);
    }

    ImU32 AccentSoft(int alpha)
    {
        return IM_COL32(100, 181, 255, alpha);
    }

    ImU32 Base(int alpha)
    {
        return IM_COL32(17, 18, 22, alpha);
    }

    ImU32 Surface(int alpha)
    {
        return IM_COL32(21, 22, 27, alpha);
    }

    ImU32 SurfaceLight(int alpha)
    {
        return IM_COL32(26, 27, 33, alpha);
    }

    ImU32 Control(int alpha)
    {
        return IM_COL32(12, 13, 17, alpha);
    }

    ImU32 Outline(int alpha)
    {
        return IM_COL32(38, 39, 47, alpha);
    }

    ImU32 Text(int alpha)
    {
        return IM_COL32(238, 238, 242, alpha);
    }

    ImU32 MutedText(int alpha)
    {
        return IM_COL32(118, 120, 126, alpha);
    }

    ImVec4 BaseVec(float alpha)
    {
        return ImVec4(0.067f, 0.071f, 0.086f, alpha);
    }

    ImVec4 SurfaceVec(float alpha)
    {
        return ImVec4(0.082f, 0.086f, 0.106f, alpha);
    }

    ImVec4 ControlVec(float alpha)
    {
        return ImVec4(0.047f, 0.051f, 0.067f, alpha);
    }

    ImVec4 AccentVec(float alpha)
    {
        return ImVec4(0.039f, 0.518f, 1.0f, alpha);
    }

    void ApplyStyle()
    {
        ImGuiStyle& style = ImGui::GetStyle();
        style.WindowRounding = 16.0f;
        style.WindowBorderSize = 0.0f;
        style.FrameRounding = 9.0f;
        style.FrameBorderSize = 0.0f;
        style.ChildRounding = 10.0f;
        style.PopupRounding = 10.0f;
        style.ScrollbarRounding = 8.0f;
        style.GrabRounding = 8.0f;
        style.WindowPadding = ImVec2(15.0f, 13.0f);
        style.FramePadding = ImVec2(11.0f, 9.0f);
        style.ItemSpacing = ImVec2(9.0f, 9.0f);
        style.ItemInnerSpacing = ImVec2(8.0f, 7.0f);
        style.Colors[ImGuiCol_WindowBg] = BaseVec();
        style.Colors[ImGuiCol_ChildBg] = BaseVec(0.72f);
        style.Colors[ImGuiCol_PopupBg] = SurfaceVec(0.98f);
        style.Colors[ImGuiCol_Border] = ImGui::ColorConvertU32ToFloat4(Outline());
        style.Colors[ImGuiCol_FrameBg] = ControlVec();
        style.Colors[ImGuiCol_FrameBgHovered] = SurfaceVec();
        style.Colors[ImGuiCol_FrameBgActive] = ImVec4(0.055f, 0.235f, 0.42f, 1.0f);
        style.Colors[ImGuiCol_CheckMark] = AccentVec();
        style.Colors[ImGuiCol_SliderGrab] = AccentVec();
        style.Colors[ImGuiCol_SliderGrabActive] = ImVec4(0.35f, 0.70f, 1.0f, 1.0f);
        style.Colors[ImGuiCol_Button] = ControlVec();
        style.Colors[ImGuiCol_ButtonHovered] = SurfaceVec();
        style.Colors[ImGuiCol_ButtonActive] = ImVec4(0.055f, 0.235f, 0.42f, 1.0f);
        style.Colors[ImGuiCol_Header] = AccentVec(0.22f);
        style.Colors[ImGuiCol_HeaderHovered] = AccentVec(0.32f);
        style.Colors[ImGuiCol_HeaderActive] = AccentVec(0.42f);
        style.Colors[ImGuiCol_Separator] = ImGui::ColorConvertU32ToFloat4(Outline(180));
        style.Colors[ImGuiCol_ScrollbarBg] = ControlVec(0.65f);
        style.Colors[ImGuiCol_ScrollbarGrab] = AccentVec(0.48f);
        style.Colors[ImGuiCol_ScrollbarGrabHovered] = AccentVec(0.68f);
        style.Colors[ImGuiCol_ScrollbarGrabActive] = AccentVec(0.82f);
    }
}
