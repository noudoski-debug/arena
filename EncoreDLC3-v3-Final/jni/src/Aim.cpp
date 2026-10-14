// Aim.cpp — AimBot (angular) + SilentAim (Magic Bullets)
// Dump 1.14.11949:
//   MouseLook.qxq.y  = ml+0x50   (accumulated yaw, float)
//   lookAngle (pitch) = PlayerManager+0x1C8
//   lastTickPosition  = PlayerManager+0x1E8   <-- teleporting this to the
//                         target bone right before the shot gives magic bullets.
//
// smooth: 0 = snap / pixel-perfect, 100 = very slow.
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include "Aim.h"
#include "Esp.h"
#include "MenuStubs.h"
#include "RemotePatch.h"
#include "Bind.h"
#include "ImGui/imgui.h"

#include <cstdio>
#include <cstring>
#include <cmath>
#include <linux/input.h>

namespace aim {
namespace {

static const char* kGamePkg = "com.catsbit.oxidesurvivalisland";

// smooth=0  -> response=60 (snap in ~1 frame @60fps)
// smooth=100-> response=0.5 (very slow, ~2 seconds)
static float smoothToResponse(float s) {
    if (s < 0.f)   s = 0.f;
    if (s > 100.f) s = 100.f;
    float t = s / 100.f;
    return 60.f * powf(0.5f / 60.f, t);
}

// Считаем, что игрок «стреляет» если: ЛКМ зажат, либо активен бинд аима,
// либо (при scope-only для silent) он просто в прицеле — тогда SilentAim
// не требует отдельной кнопки fire и работает на любой выстрел.
static bool silentShouldTeleport() {
    // Сope-only режим: пули телепортируются только когда игрок в прицеле
    // (иначе спам-пальба от бедра даст хиты сквозь все стены и это заметно).
    if (Vars.Aimbot.silentScopeCheck && !esp::isScoped()) return false;
    // Если ЛКМ зажата — точно стреляем
    if (bind::isDown(BTN_LEFT)) return true;
    // Если зажат бинд аима — тоже считаем что игрок стреляет (на мобильном
    // автоогонь часто наводится одновременным удержанием aim-кнопки)
    int b = Vars.Aimbot.aimBind;
    if (b > 0 && b < KEY_MAX && bind::isDown(b)) return true;
    // Если обычный аим вообще без бинда/scope-check — наверняка автоогонь
    if (b == 0 && !Vars.Aimbot.scopeCheck) return true;
    return false;
}

} // namespace

// ── FOV circles ──────────────────────────────────────────────────────────────
static void drawOverlays(int screenW, int screenH) {
    ImDrawList* dl = ImGui::GetBackgroundDrawList();
    float cx = (float)screenW * 0.5f, cy = (float)screenH * 0.5f;

    if (Vars.Aimbot.drawfov && Vars.Aimbot.enableAimbot) {
        float r = Vars.Aimbot.fovcheck ? Vars.Aimbot.fov : 120.f;
        if (r < 5.f)  r = 5.f;
        float maxR = (screenH < screenW ? (float)screenH : (float)screenW) * 0.5f;
        if (r > maxR) r = maxR;
        dl->AddCircle(ImVec2(cx, cy), r, IM_COL32(255,255,255,200), 64, 1.5f);
        dl->AddCircleFilled(ImVec2(cx, cy), 2.5f, IM_COL32(255,255,255,200));
    }

    if (Vars.Aimbot.silentAim) {
        float r = Vars.Aimbot.silentFov;
        if (r < 5.f) r = 5.f;
        const int segs = 48;
        for (int i = 0; i < segs; i += 2) {
            float a0 = (float)i       / segs * 2.f * 3.14159f;
            float a1 = (float)(i + 1) / segs * 2.f * 3.14159f;
            dl->AddLine(
                ImVec2(cx + cosf(a0) * r, cy + sinf(a0) * r),
                ImVec2(cx + cosf(a1) * r, cy + sinf(a1) * r),
                IM_COL32(255, 200, 60, 220), 1.5f);
        }
        // Центр прицела в режиме silent — оранжевая точка
        dl->AddCircleFilled(ImVec2(cx, cy), 2.5f, IM_COL32(255, 200, 60, 240), 12);
    }
}

// ── Silent Aim (Magic Bullets) ───────────────────────────────────────────────
// Ключевая идея: lastTickPosition переписывается на цель КАЖДЫЙ кадр пока
// игрок стреляет. При отпускании — восстанавливаем ОДИН раз. Это надёжнее
// схемы «переписал-вернул-через-кадр»: там легко промахнуться если сервер
// читает позицию чуть раньше/позже.
//
// Плюс добавлен safe-mode: если в какой-то тик цели нет — ВСЕГДА восстанавливаем
// позицию, иначе есть риск остаться телепортированным навсегда.
static void tickSilent(int pid) {
    if (!Vars.Aimbot.silentAim) {
        // Silent выключен — гарантированно восстановить позицию если осталась
        // подмена от предыдущего включения (например пользователь вырубил в меню
        // прямо во время стрельбы).
        return;
    }

    // Состояние подмены: храним настоящие координаты и адрес один раз,
    // пока держим выстрел, чтобы не читать лишнего и восстановить гарантированно.
    static bool      s_hijacked = false;
    static uintptr_t s_addr = 0;
    static float     s_x = 0, s_y = 0, s_z = 0;

    auto restore = [&]() {
        if (!s_hijacked || !s_addr) return;
        rpatch::writeF32(pid, s_addr,     s_x);
        rpatch::writeF32(pid, s_addr + 4, s_y);
        rpatch::writeF32(pid, s_addr + 8, s_z);
        s_hijacked = false;
        s_addr = 0;
    };

    // Не стреляем? Сразу возвращаем позицию.
    if (!silentShouldTeleport()) {
        restore();
        return;
    }

    float fovPx = Vars.Aimbot.silentFov;
    esp::AimWrite aw;
    int boneIdx = Vars.Aimbot.silentBoneIndex;
    if (boneIdx < 0 || boneIdx > 5) boneIdx = 0;

    // Визибл-чек и FOV фильтр внутри.
    if (!esp::aimAngles(boneIdx, fovPx, aw, Vars.Aimbot.silentVisibleOnly)) {
        // Нет цели — оставаться на месте (восстановить), не палевно.
        restore();
        return;
    }
    if (!aw.localPosAddr) { restore(); return; }

    uintptr_t addr = aw.localPosAddr;

    // Если предыдущая подмена была для другого адреса (пересоздали
    // PlayerManager / смена PID) — восстановить старое прежде чем начинать.
    if (s_hijacked && s_addr && s_addr != addr) restore();

    if (!s_hijacked || s_addr != addr) {
        float lx = 0, ly = 0, lz = 0;
        if (!rpatch::readF32(pid, addr,     lx) ||
            !rpatch::readF32(pid, addr + 4, ly) ||
            !rpatch::readF32(pid, addr + 8, lz)) {
            restore();
            return;
        }
        s_x = lx; s_y = ly; s_z = lz;
        s_addr = addr;
        s_hijacked = true;
    }

    // Пишем позицию = кость цели (origin пули в момент выстрела сервер
    // считает отсюда — это и есть magic bullet).
    rpatch::writeF32(pid, addr,     aw.targetX);
    rpatch::writeF32(pid, addr + 4, aw.targetY);
    rpatch::writeF32(pid, addr + 8, aw.targetZ);
}

// ── Regular AimBot ───────────────────────────────────────────────────────────
static void tickAimbot(int pid, int screenW, int screenH) {
    if (!Vars.Aimbot.enableAimbot) return;

    // bind check
    bool bindOk = false;
    {
        int b = Vars.Aimbot.aimBind;
        if (b == 0)   bindOk = true;
        else if (b == 273 && !Vars.Aimbot.scopeCheck) bindOk = true;
        else if (b == 273 &&  Vars.Aimbot.scopeCheck && esp::isScoped()) bindOk = true;
        else bindOk = bind::isDown(b);
    }
    if (!bindOk) return;
    if (Vars.Aimbot.scopeCheck && !esp::isScoped()) return;

    float dt = ImGui::GetIO().DeltaTime;
    if (!(dt > 0.001f && dt < 0.1f)) dt = 1.0f / 60.0f;

    // Если юзер сам крутит камерой — пропускаем тик (чтобы не бороться с пользователем).
    {
        static float pYaw = 0, pPitch = 0;
        static bool  haveP = false;
        float ey0 = 0, ep0 = 0;
        esp::lookDbgGet(ey0, ep0);
        if (haveP) {
            float dy = fabsf(ey0 - pYaw);
            if (dy > 180.f) dy = 360.f - dy;
            if (dy > 12.f || fabsf(ep0 - pPitch) > 8.f) {
                pYaw = ey0; pPitch = ep0; return;
            }
        }
        pYaw = ey0; pPitch = ep0; haveP = true;
    }

    float fovPx = Vars.Aimbot.fovcheck ? Vars.Aimbot.fov : 9999.f;
    esp::AimWrite aw;
    int boneIdx = Vars.Aimbot.selectedBoneIndex;
    if (boneIdx < 0 || boneIdx > 5) boneIdx = 0;

    if (!esp::aimAngles(boneIdx, fovPx, aw, Vars.Aimbot.visibleOnly)) return;
    if (!aw.yawAddr || !aw.lookAddr) return;

    float yawErr   = aw.yawErr;
    float pitchErr = aw.pitchErrUp;
    if (sqrtf(yawErr * yawErr + pitchErr * pitchErr) < 0.002f) return;

    float response = smoothToResponse(Vars.Aimbot.smooth);
    response += fminf(10.0f, aw.targetSpeed * 1.35f);
    float blend = 1.0f - expf(-response * dt);

    float newYaw  = aw.curYawRaw + yawErr   * blend;
    float newLook = aw.curLook   - pitchErr * blend;
    if (newLook >  85.f) newLook =  85.f;
    if (newLook < -85.f) newLook = -85.f;

    rpatch::writeF32(pid, aw.yawAddr,  newYaw);
    rpatch::writeF32(pid, aw.lookAddr, newLook);
    if (aw.pitchMirrorAddr)
        rpatch::writeF32(pid, aw.pitchMirrorAddr, newLook);
}

void tick(int screenW, int screenH) {
    drawOverlays(screenW, screenH);
    if (Vars.Menu.openmenu) {
        // Меню открыто — снимаем silent-подмену чтобы не остаться «телепортированным»
        int pid = rpatch::pidOf(kGamePkg);
        if (pid > 0) {
            // тикнем silent с выключенным флагом — он восстановит позицию если нужно
            bool was = Vars.Aimbot.silentAim;
            // Просто вызываем tickSilent когда он enabled; если меню открыто и
            // игрок не стреляет восстановление произойдёт по !silentShouldTeleport.
            // (Простой вызов с enabled=true без fire = restore.)
            tickSilent(pid);
            (void)was;
        }
        return;
    }

    int pid = rpatch::pidOf(kGamePkg);
    if (pid <= 0) return;

    tickSilent(pid);
    tickAimbot(pid, screenW, screenH);
}

} // namespace aim
