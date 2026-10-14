#include "encore/LitvinMenu.h"
#include "Runtime.h"
#include "Menu.h"
#include "Target.h"
#include <algorithm>

AllVars Vars;
namespace menu {
void render() {
    ImGuiIO& io = ImGui::GetIO();
    const float width = io.DisplaySize.x, height = io.DisplaySize.y;
    const runtime::Frame frame = runtime::snapshot();
    ImDrawList* dl = ImGui::GetBackgroundDrawList();
    if (Vars.ESP.enable) {
        if (Vars.ESP.count) {
            char text[64]; std::snprintf(text, sizeof(text), "Players tracked: %u", frame.players);
            dl->AddText(ImVec2(width * 0.5f - 80.f, 18.f), IM_COL32_WHITE, text);
        }
        for (const auto& box : frame.boxes) {
            if (!box.projected) continue;
            float top = box.headY*height, bottom = box.feetY*height;
            const float boxHeight = bottom-top;
            if (!std::isfinite(boxHeight) || boxHeight < 2.f || boxHeight > height*4.f) continue;
            float halfWidth = boxHeight * 0.325f, center = box.headX*width;
            if (center+halfWidth < 0 || center-halfWidth > width || bottom < 0 || top > height) continue;
            const ImVec2 lo(center-halfWidth, top), hi(center+halfWidth, bottom);
            const ImU32 color = ImGui::ColorConvertFloat4ToU32(ImVec4(Vars.ESP.boxColor[0],Vars.ESP.boxColor[1],Vars.ESP.boxColor[2],1));
            if (Vars.ESP.box) {
                if (Vars.ESP.xrayFill) dl->AddRectFilled(lo, hi, (color & 0x00FFFFFF) | (32u<<24), Vars.Settings.espround);
                if (Vars.ESP.outline) dl->AddRect(lo, hi, IM_COL32(0,0,0,220), Vars.Settings.espround, 0, 3.5f);
                dl->AddRect(lo, hi, color, Vars.Settings.espround, 0, 1.5f);
            }
            if (Vars.ESP.tracer) {
                const auto* c = Vars.ESP.lineColor;
                dl->AddLine(ImVec2(width*0.5f, 0), ImVec2(center, top),
                            ImGui::ColorConvertFloat4ToU32(ImVec4(c[0],c[1],c[2],1)), Vars.ESP.lineThickness);
            }
            std::string label = Vars.ESP.name ? box.label : "";
            if (Vars.ESP.distance && std::isfinite(box.distance)) {
                char distance[32]; std::snprintf(distance, sizeof(distance), " %.0fm", box.distance);
                label += distance;
            }
            if (!label.empty()) dl->AddText(ImVec2(center-ImGui::CalcTextSize(label.c_str()).x*0.5f, top-ImGui::GetFontSize()-2.f), color, label.c_str());
        }
    }
    ImGui::SetNextWindowPos(ImVec2(12, 12), ImGuiCond_Always);
    ImGui::SetNextWindowBgAlpha(0.9f);
    ImGui::Begin("DearVision status", nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings);
    if (ImGui::Button("DearVision / INTERNAL ROOT", ImVec2(320, 42))) LitvinMenu::open = !LitvinMenu::open;
    ImGui::Text("Build: %s", target::Version);
    ImGui::TextUnformatted(runtime::status());
    ImGui::End();
    LitvinMenu::Render();
    if (LitvinMenu::ShouldExit()) Vars.ESP.enable = false; // NEVER exit() the host game.
    runtime::setEnabled(Vars.ESP.enable);
}
}
