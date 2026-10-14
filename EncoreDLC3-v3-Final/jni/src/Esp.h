#pragma once
// Esp.h — внешний ESP для Oxide
#include <cstdint>
#include <string>

namespace esp {
void tick(int screenW, int screenH);

struct AimWrite {
    uint64_t yawAddr         = 0;
    uint64_t pitchMirrorAddr = 0;
    uint64_t lookAddr        = 0;
    uint64_t localPosAddr    = 0;   // адрес lastTickPosition локального игрока
    float curYawRaw  = 0;
    float curLook    = 0;
    float desYaw     = 0;
    float desLook    = 0;
    float yawErr     = 0;
    float pitchErrUp = 0;
    float targetSpeed = 0;
    float targetX    = 0;           // позиция цели X (для SilentAim)
    float targetY    = 0;           // позиция цели Y
    float targetZ    = 0;           // позиция цели Z
};

// visibleOnly=true — цель должна быть на экране и видимой (e.visible).
bool aimAngles(int boneIdx, float fovPx, AimWrite& out, bool visibleOnly = false);
bool isScoped();
void lookDbgGet(float& yaw, float& pitch);
void setDebug(bool on);
} // namespace esp

// Глобальная функция для FreeCam: возвращает адрес локального PlayerManager.
// Определена в Esp.cpp, обновляется в esp::tick().
uintptr_t esp_getLocalPM();
