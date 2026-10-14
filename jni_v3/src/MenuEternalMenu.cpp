#include "menu.hpp"
#include "LitvinMenu.h"
#include "Vars.h"
#include "Bind.h"
#include "imgui.h"

// LitvinMenu — header-only.
namespace menu {
namespace {

// Новый современный watermark: тёмная карточка с неоновым акцентом,
// никаких «LITVIN TAP / ANDROID» и AI-лейблов.
void drawWatermark() {
    if (!Vars.Menu.watera) return;
    ImDrawList* d = ImGui::GetBackgroundDrawList();
    const float t = (float)ImGui::GetTime();

    // Размеры карточки
    const ImVec2 p(12.0f, 12.0f);
    const float w = 190.0f, h = 50.0f;
    const ImVec2 q(p.x + w, p.y + h);
    const float r = 10.0f;

    // Внешнее свечение (акцент)
    d->AddRectFilled(ImVec2(p.x-4, p.y-4), ImVec2(q.x+4, q.y+4),
                     IM_COL32(88, 166, 255, 26), r + 4.0f);
    // Основа
    d->AddRectFilled(p, q, IM_COL32(14, 16, 22, 232), r);
    // Верхняя подсветка
    d->AddRectFilledMultiColor(
        ImVec2(p.x + 4, p.y + 1), ImVec2(q.x - 4, p.y + 5),
        IM_COL32(120, 190, 255, 78), IM_COL32(120, 190, 255, 18),
        IM_COL32(120, 190, 255, 18), IM_COL32(120, 190, 255, 78));
    // Рамка
    d->AddRect(p, q, IM_COL32(88, 166, 255, 210), r, 0, 1.2f);

    // Лого-маркер слева: круг с пульсирующей точкой
    const float cx = p.x + 22.0f, cy = p.y + h * 0.5f;
    const float pulse = 0.75f + 0.25f * sinf(t * 3.0f);
    d->AddCircleFilled(ImVec2(cx, cy), 10.0f, IM_COL32(88, 166, 255, 60), 28);
    d->AddCircleFilled(ImVec2(cx, cy), 6.0f, IM_COL32(88, 166, 255, 100), 28);
    d->AddCircleFilled(ImVec2(cx, cy), 3.0f + pulse * 1.2f,
                       IM_COL32(230, 245, 255, 230), 20);

    // Заголовок
    d->AddText(ImVec2(p.x + 42.0f, p.y + 9.0f),
               IM_COL32(245, 248, 255, 255), "EncoreDLC");
    // Подзаголовок (версия/статус) — без упоминаний платформы/автора/AI
    char sub[48];
    snprintf(sub, sizeof(sub), "v3  |  external");
    d->AddText(ImVec2(p.x + 42.0f, p.y + 28.0f),
               IM_COL32(140, 180, 230, 255), sub);

    // Акцентная полоска справа
    d->AddRectFilled(ImVec2(q.x - 3.0f, p.y + 6.0f),
                     ImVec2(q.x - 1.0f, q.y - 6.0f),
                     IM_COL32(88, 166, 255, 220), 1.0f);
}

} // namespace

void render() {
    if (bind::consumeMenuToggle()) Vars.Menu.openmenu = !Vars.Menu.openmenu;

    // Тап по вотермарке тоже открывает/закрывает меню
    if (Vars.Menu.watera && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
        const ImVec2 mp = ImGui::GetIO().MousePos;
        if (mp.x >= 8.0f && mp.x <= 210.0f && mp.y >= 8.0f && mp.y <= 70.0f)
            Vars.Menu.openmenu = !Vars.Menu.openmenu;
    }

    drawWatermark();

    LitvinMenu::open = Vars.Menu.openmenu;
    LitvinMenu::Render();
    Vars.Menu.openmenu = LitvinMenu::open;
}

} // namespace menu
