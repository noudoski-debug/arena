#pragma once
// Aim.h — угловой аим: пишем yaw/pitch в память игры (ml+0x50 / +0x1A8).
// Тач используется только для тапа кнопки огня (триггер).
namespace aim {
// Каждый кадр из main loop. screenW/H — экран.
void tick(int screenW, int screenH);
} // namespace aim
