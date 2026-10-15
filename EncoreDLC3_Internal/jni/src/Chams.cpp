// Chams.cpp — overlay chams (рисует силуэты через DrawList)
// Три материала: Flat, Glow, Wireframe
// throughWalls: рисует даже невидимых (отдельный цвет)
#include "Chams.h"
#include "Vars.h"
#include <cmath>
#include <algorithm>

namespace chams {

static void drawFlat(ImDrawList* dl, const ChamsPlayer& p,
                     ImU32 col, float rounding)
{
    float x0 = p.screenX - p.screenW * 0.5f;
    float y0 = p.screenY - p.screenH;
    float x1 = p.screenX + p.screenW * 0.5f;
    float y1 = p.screenY;
    dl->AddRectFilled(ImVec2(x0,y0), ImVec2(x1,y1), col, rounding);
}

static void drawGlow(ImDrawList* dl, const ChamsPlayer& p,
                     ImU32 col, float rounding)
{
    float x0 = p.screenX - p.screenW * 0.5f;
    float y0 = p.screenY - p.screenH;
    float x1 = p.screenX + p.screenW * 0.5f;
    float y1 = p.screenY;
    // Многослойное свечение
    ImVec4 cv = ImGui::ColorConvertU32ToFloat4(col);
    for (int i = 6; i >= 1; i--) {
        float e = (float)i * 4.f;
        ImVec4 gc = cv;
        gc.w = cv.w * ((float)(6-i+1) / 12.f);
        dl->AddRectFilled(
            ImVec2(x0-e, y0-e), ImVec2(x1+e, y1+e),
            ImGui::ColorConvertFloat4ToU32(gc), rounding + e);
    }
    dl->AddRectFilled(ImVec2(x0,y0), ImVec2(x1,y1), col, rounding);
}

static void drawWireframe(ImDrawList* dl, const ChamsPlayer& p,
                          ImU32 col, float rounding)
{
    float x0 = p.screenX - p.screenW * 0.5f;
    float y0 = p.screenY - p.screenH;
    float x1 = p.screenX + p.screenW * 0.5f;
    float y1 = p.screenY;
    // Двойной прямоугольник (имитация wireframe на overlay)
    dl->AddRect(ImVec2(x0-1,y0-1), ImVec2(x1+1,y1+1), col, rounding, 0, 3.f);
    dl->AddRect(ImVec2(x0+1,y0+1), ImVec2(x1-1,y1-1), col, rounding, 0, 1.f);
    // Горизонтальные линии сканлайна
    int lines = std::max(1, (int)(p.screenH / 6.f));
    for (int i = 0; i <= lines; i++) {
        float ly = y0 + (float)i / lines * p.screenH;
        dl->AddLine(ImVec2(x0,ly), ImVec2(x1,ly), col & 0x80FFFFFF, 0.8f);
    }
}

void draw(ImDrawList* dl, const ChamsPlayer* players, int count) {
    if (!Vars.Chams.enable || count == 0) return;

    float r = Vars.Chams.r, g = Vars.Chams.g, b = Vars.Chams.b;
    float wr= Vars.Chams.wr, wg = Vars.Chams.wg, wb = Vars.Chams.wb;
    float a = Vars.Chams.opacity;

    const float rounding = 3.f;

    for (int i = 0; i < count; i++) {
        const ChamsPlayer& cp = players[i];
        if (cp.screenH < 4.f || cp.screenW < 2.f) continue;

        bool drawThisOne = cp.visible || Vars.Chams.throughWalls;
        if (!drawThisOne) continue;

        // Цвет: видимые = основной, через стены = красноватый
        float cr, cg, cb;
        if (cp.visible) { cr = r; cg = g; cb = b; }
        else            { cr = wr; cg = wg; cb = wb; }

        ImU32 col = IM_COL32(
            (int)(cr * 255), (int)(cg * 255), (int)(cb * 255), (int)(a * 255));

        switch (Vars.Chams.material) {
        default:
        case 0: drawFlat(dl, cp, col, rounding);       break;
        case 1: drawGlow(dl, cp, col, rounding);       break;
        case 2: drawWireframe(dl, cp, col, rounding);  break;
        }
    }
}

} // namespace chams
