#pragma once
// FreeCam.h — визуальный FreeCam для Oxide Survival Island
// Находит Unity Camera через FPManager.m_WorldCamera (PlayerManager+0x90 -> FPManager+0x20)
// Пишет Transform.position/rotation только в память — игра не узнаёт (сервер не видит).
// Управление: джойстик в ImGui (появляется когда камера найдена + FreeCam включён).

namespace freecam {

// Вызывать каждый кадр ВНУТРИ ImGui-фрейма.
// screenW/H — размер поверхности.
void tick(int screenW, int screenH);

} // namespace freecam
