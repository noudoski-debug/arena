// FreeCam.cpp — визуальный FreeCam (только оверлей, сервер не видит)
// Dump 1.14.11949 оффсеты:
//   PlayerManager.fpManager   = +0x90   (FPManager*)
//   FPManager.m_WorldCamera   = +0x20   (Camera*)  — Unity Camera объект
//   Camera -> GameObject -> Transform chain (стандартный Unity IL2CPP):
//     Camera.m_CachedPtr (MonoBehaviour) -> native Transform через GetTransform()
//     Используем прямую цепочку managed->native:
//       Camera obj  +0x10  -> GameObject ptr
//       GameObject  +0x30  -> Transform managed
//       Transform   +0x10  -> native TransformData ptr (td)
//       td          +0x90  -> world position (Vector3, confirmed by Esp.cpp chain)
//       rotation через: td+0x70 -> Quaternion  (world rotation stored as quat)
//
// Управление джойстиком:
//   Левый джойстик  = XZ движение (горизонталь)
//   Правый джойстик = поворот yaw/pitch
//   Слайдер скорости в меню
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include "FreeCam.h"
#include "Esp.h"
#include "RemotePatch.h"
#include "MenuStubs.h"
#include "ImGui/imgui.h"

#include <cstdio>
#include <cmath>
#include <cstring>

namespace freecam {
namespace {

static const char* kGamePkg = "com.catsbit.oxidesurvivalisland";

// Оффсеты (dump 1.14.11949)
static const uintptr_t kFpManagerOff     = 0x90;  // PlayerManager -> FPManager
static const uintptr_t kWorldCameraOff   = 0x20;  // FPManager -> Camera
static const uintptr_t kGoOff            = 0x10;  // Camera -> GameObject (managed base)
static const uintptr_t kTransformOff     = 0x30;  // GameObject -> Transform managed
static const uintptr_t kTdOff            = 0x10;  // Transform managed -> native td ptr
static const uintptr_t kPosTdOff         = 0x90;  // td -> world position Vector3
static const uintptr_t kRotTdOff         = 0x70;  // td -> world rotation Quaternion (x,y,z,w)

// Состояние
static uintptr_t s_camObj    = 0;   // адрес Camera managed object
static uintptr_t s_td        = 0;   // native TransformData*
static float     s_origPos[3]= {};  // исходная позиция (для восстановления)
static float     s_origRot[4]= {};  // исходный кватернион
static bool      s_saved     = false;

// Эйлер -> кватернион (Y→X→Z порядок Unity)
static void eulerToQuat(float yawDeg, float pitchDeg, float q[4]) {
    const float kD2R = 3.14159265f / 180.f;
    float hy = yawDeg   * kD2R * 0.5f;
    float hp = pitchDeg * kD2R * 0.5f;
    // Unity: Quaternion.Euler(pitch, yaw, 0)
    float cy = cosf(hy), sy = sinf(hy);
    float cp = cosf(hp), sp = sinf(hp);
    q[0] = sp * cy;          // x
    q[1] = cp * sy;          // y
    q[2] = -sp * sy;         // z
    q[3] = cp * cy;          // w
}

// Читаем local PlayerManager (isLocalPlayer флаг)
static uintptr_t s_localPm = 0;
static int        s_pid     = -1;

static bool resolveCamera(int pid) {
    // esp_getLocalPM() объявлена в Esp.h, определена в Esp.cpp.
    uintptr_t pm = esp_getLocalPM();
    if (!pm) return false;

    uint64_t fpMgr = 0;
    if (!rpatch::readU64(pid, pm + kFpManagerOff, fpMgr) || !fpMgr) return false;

    uint64_t cam = 0;
    if (!rpatch::readU64(pid, fpMgr + kWorldCameraOff, cam) || !cam) return false;

    // Camera -> GameObject -> Transform managed -> td
    uint64_t go = 0, trans = 0, td = 0;
    if (!rpatch::readU64(pid, cam + kGoOff, go) || !go) return false;
    if (!rpatch::readU64(pid, go  + kTransformOff, trans) || !trans) return false;
    if (!rpatch::readU64(pid, trans + kTdOff, td) || !td) return false;

    s_camObj = (uintptr_t)cam;
    s_td     = (uintptr_t)td;
    return true;
}

static bool readPos(int pid, float pos[3]) {
    return rpatch::readMem(pid, s_td + kPosTdOff, pos, 12);
}
static bool readRot(int pid, float rot[4]) {
    return rpatch::readMem(pid, s_td + kRotTdOff, rot, 16);
}
static bool writePos(int pid, const float pos[3]) {
    return rpatch::writeMem(pid, s_td + kPosTdOff, pos, 12);
}
static bool writeRot(int pid, const float rot[4]) {
    return rpatch::writeMem(pid, s_td + kRotTdOff, rot, 16);
}

// Маленький джойстик: возвращает смещение в [-1,1] по X и Y
struct JoyResult { float x, y; bool touched; };

static JoyResult DrawJoystick(const char* id, ImVec2 center, float radius,
                               ImU32 bgCol, ImU32 handleCol)
{
    ImDrawList* d  = ImGui::GetBackgroundDrawList();
    ImGuiIO&    io = ImGui::GetIO();

    d->AddCircleFilled(center, radius, bgCol, 40);
    d->AddCircle(center, radius, IM_COL32(200,200,200,120), 40, 1.5f);

    JoyResult r = {0,0,false};

    // Определяем активный тач внутри джойстика
    static int s_touch[2] = {-1,-1}; // [0]=move [1]=look
    int slot = (id[0]=='m') ? 0 : 1;

    for (int i = 0; i < IM_ARRAYSIZE(io.MouseDown); i++) {
        if (io.MouseDown[i]) {
            float dx = io.MousePos.x - center.x;
            float dy = io.MousePos.y - center.y;
            if (sqrtf(dx*dx+dy*dy) <= radius * 1.4f || s_touch[slot] == i) {
                s_touch[slot] = i;
                float nx = dx / radius, ny = dy / radius;
                float len = sqrtf(nx*nx+ny*ny);
                if (len > 1.f) { nx /= len; ny /= len; }
                r.x = nx; r.y = ny; r.touched = true;

                // рисуем ручку
                d->AddCircleFilled(
                    ImVec2(center.x + nx*radius*0.6f, center.y + ny*radius*0.6f),
                    radius * 0.35f, handleCol, 32);
                break;
            }
        }
    }
    if (!r.touched) {
        s_touch[slot] = -1;
        d->AddCircleFilled(center, radius * 0.35f, handleCol, 32);
    }
    return r;
}

} // namespace

void tick(int screenW, int screenH) {
    if (!Vars.FreeCam.enable) {
        // Если отключили — восстановить исходное положение
        if (s_saved && s_td) {
            int pid = rpatch::pidOf(kGamePkg);
            if (pid > 0) {
                writePos(pid, s_origPos);
                writeRot(pid, s_origRot);
            }
            s_saved = false;
        }
        Vars.FreeCam.found = false;
        s_td = 0;
        return;
    }

    int pid = rpatch::pidOf(kGamePkg);
    if (pid <= 0) { Vars.FreeCam.found = false; return; }

    // Переинициализируем если pid сменился или td сбросился
    if (pid != s_pid || !s_td) {
        s_pid = pid;
        s_td  = 0;
        s_saved = false;
        if (!resolveCamera(pid)) {
            Vars.FreeCam.found = false;
            return;
        }
    }

    // Пробуем прочитать позицию — если не вышло, td протух
    float curPos[3];
    if (!readPos(pid, curPos)) {
        s_td = 0; Vars.FreeCam.found = false; return;
    }

    Vars.FreeCam.found = true;

    // Сохраняем исходную позицию один раз
    if (!s_saved) {
        memcpy(s_origPos, curPos, 12);
        readRot(pid, s_origRot);
        Vars.FreeCam.posX  = curPos[0];
        Vars.FreeCam.posY  = curPos[1];
        Vars.FreeCam.posZ  = curPos[2];
        s_saved = true;
        // Инициализируем yaw из текущего угла камеры (упрощённо = 0)
        Vars.FreeCam.yaw   = 0.f;
        Vars.FreeCam.pitch = 0.f;
    }

    // ── UI: статус-бейдж ─────────────────────────────────────────────────
    {
        ImDrawList* d = ImGui::GetBackgroundDrawList();
        char buf[64];
        snprintf(buf, sizeof buf, "FreeCam  [%.0f %.0f %.0f]",
                 Vars.FreeCam.posX, Vars.FreeCam.posY, Vars.FreeCam.posZ);
        ImVec2 ts = ImGui::CalcTextSize(buf);
        float px = (float)screenW * 0.5f - ts.x * 0.5f;
        float py = 18.f;
        d->AddRectFilled(ImVec2(px-8,py-4), ImVec2(px+ts.x+8,py+ts.y+4),
                         IM_COL32(10,10,20,210), 6.f);
        d->AddRect(ImVec2(px-8,py-4), ImVec2(px+ts.x+8,py+ts.y+4),
                   IM_COL32(80,180,255,180), 6.f, 0, 1.2f);
        d->AddText(ImVec2(px,py), IM_COL32(255,255,255,255), buf);
    }

    // ── Джойстики ─────────────────────────────────────────────────────────
    const float jR  = 60.f;   // радиус
    const float mX  = (float)screenW  * 0.20f;
    const float mY  = (float)screenH  - jR - 40.f;
    const float lX  = (float)screenW  * 0.80f;
    const float lY  = mY;

    // Движение: левый джойстик
    JoyResult move = DrawJoystick("move", ImVec2(mX, mY), jR,
                                  IM_COL32(40,40,60,200), IM_COL32(80,160,255,230));
    // Камера: правый джойстик
    JoyResult look = DrawJoystick("look", ImVec2(lX, lY), jR,
                                  IM_COL32(40,40,60,200), IM_COL32(255,160,80,230));

    float dt = ImGui::GetIO().DeltaTime;
    if (!(dt > 0.001f && dt < 0.2f)) dt = 1.f/60.f;
    float spd = Vars.FreeCam.speed;

    // Поворот (правый)
    if (look.touched) {
        const float rotSpd = 80.f; // °/sec
        Vars.FreeCam.yaw   += look.x * rotSpd * dt;
        Vars.FreeCam.pitch  -= look.y * rotSpd * dt;
        if (Vars.FreeCam.pitch >  89.f) Vars.FreeCam.pitch =  89.f;
        if (Vars.FreeCam.pitch < -89.f) Vars.FreeCam.pitch = -89.f;
    }

    // Движение (левый), в плоскости yaw
    if (move.touched) {
        const float kD2R = 3.14159265f / 180.f;
        float yR = Vars.FreeCam.yaw * kD2R;
        float fwdX =  sinf(yR), fwdZ = cosf(yR);
        float rigX =  cosf(yR), rigZ = -sinf(yR);
        Vars.FreeCam.posX += (-move.y * fwdX + move.x * rigX) * spd * dt;
        Vars.FreeCam.posZ += (-move.y * fwdZ + move.x * rigZ) * spd * dt;
        // Вертикаль: слайдер держит Y, можно добавить вертикальные кнопки
    }

    // Пишем позицию и поворот в память
    float newPos[3] = { Vars.FreeCam.posX, Vars.FreeCam.posY, Vars.FreeCam.posZ };
    float quat[4];
    eulerToQuat(Vars.FreeCam.yaw, Vars.FreeCam.pitch, quat);

    writePos(pid, newPos);
    writeRot(pid, quat);

    // Обновляем curPos чтобы s_saved не сбрасывалось
    memcpy(curPos, newPos, 12);
}

} // namespace freecam
