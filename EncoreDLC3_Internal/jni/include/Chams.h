#pragma once
// Chams.h — визуальные Chams (overlay-рисование силуэтов игроков)
// Работает через ImGui DrawList: рисует закрашенный прямоугольник по позиции каждого
// видимого/невидимого игрока ПОВЕРХ/ПОД нормальным ESP.
// Вызывать из esp::tick() после основного прохода по игрокам.

#include "ImGui/imgui.h"

namespace chams {

struct ChamsPlayer {
    float screenX, screenY;    // центр ног
    float screenW, screenH;    // ширина/высота box на экране
    bool  visible;             // видит ли его основная камера
};

// Вызывается каждый кадр. players — список игроков с экранными координатами.
// Размер списка — count.
void draw(ImDrawList* dl, const ChamsPlayer* players, int count);

} // namespace chams
