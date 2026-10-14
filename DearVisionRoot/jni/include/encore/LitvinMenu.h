// ============================================================================
//  LitvinMenu.h  —  LITVIN TAP style  |  EncoreDLC Edition  (rewritten)
//  Функции: AimBot, SilentAim, No Recoil, No Spread, FOV Arms, Custom FOV,
//           ESP (Box/Name/Dist/Tracer/XRay/Count/Arrows),
//           Chams (Flat/Glow/Wireframe, Through Walls),
//           Hit Effects,  FreeCam (Enable + Speed + джойстик),
//           Settings (Theme/Config/ESPRound)
// ============================================================================
#pragma once
#define IMGUI_DEFINE_MATH_OPERATORS
#include "ImGui/imgui.h"
#include "ImGui/imgui_internal.h"
#include <algorithm>
#include <string>
#include <vector>
#include "Vars.h"
#include "HitEffect.h"
#include <cstring>
#include <cmath>
#include <cstdio>
#include <unordered_map>

namespace LitvinMenu {

inline bool   open  = false;
inline float  scale = 1.0f;
inline void (*OnFolder)() = nullptr;

inline void LoadFonts(const char*, const char*, const char*) {}
inline void LoadFontsMemory(void*,int,void*,int,void*,int) {}

// ============================================================================
//  PALETTE
// ============================================================================
namespace pal {
struct Theme {
    ImU32 BG, SIDE, BTN, BTN_H, TAB, PANEL;
    ImU32 TRACK, TRACK_H;
    ImU32 TXT, TXT_H, DIM, LINE, BORDER, VAL, NAV, NAV_H, INPUT;
    ImU32 ACCENT, ACCENT_H;
};
inline Theme Dark() {
    auto H=[](unsigned h,int a=255)->ImU32{return IM_COL32((h>>16)&255,(h>>8)&255,h&255,a);};
    return {H(0x1a1b20),H(0x16171b),H(0x202127),H(0x26272d),H(0x1d1e23),H(0x1e1f24),
            H(0x2a2b31),H(0x33343b),H(0x8e9097),H(0xb5b7bd),H(0x5e6067),H(0x24252b),
            H(0x2a2b31),H(0xd6d7db),H(0xa3a5ac),H(0xd0d2d7),H(0xdcdde1),H(0x3b82f6),H(0x60a5fa)};
}
inline Theme Blue() {
    return {IM_COL32(14,20,40,255),IM_COL32(10,15,32,255),IM_COL32(20,30,60,255),IM_COL32(28,40,78,255),
            IM_COL32(16,24,50,255),IM_COL32(18,26,54,255),IM_COL32(30,45,80,255),IM_COL32(40,58,100,255),
            IM_COL32(130,150,200,255),IM_COL32(180,200,240,255),IM_COL32(70,90,140,255),
            IM_COL32(25,35,65,255),IM_COL32(35,50,85,255),IM_COL32(200,215,245,255),
            IM_COL32(140,165,220,255),IM_COL32(190,210,250,255),IM_COL32(210,225,255,255),
            IM_COL32(56,189,248,255),IM_COL32(125,211,252,255)};
}
inline Theme Red() {
    return {IM_COL32(26,14,14,255),IM_COL32(20,10,10,255),IM_COL32(38,20,20,255),IM_COL32(50,26,26,255),
            IM_COL32(32,16,16,255),IM_COL32(34,18,18,255),IM_COL32(52,28,28,255),IM_COL32(66,36,36,255),
            IM_COL32(190,140,140,255),IM_COL32(230,190,190,255),IM_COL32(120,70,70,255),
            IM_COL32(44,22,22,255),IM_COL32(58,30,30,255),IM_COL32(245,210,210,255),
            IM_COL32(200,140,140,255),IM_COL32(240,190,190,255),IM_COL32(250,220,220,255),
            IM_COL32(224,62,62,255),IM_COL32(248,113,113,255)};
}
inline Theme Green() {
    return {IM_COL32(14,26,16,255),IM_COL32(10,20,12,255),IM_COL32(18,36,22,255),IM_COL32(24,48,28,255),
            IM_COL32(16,30,18,255),IM_COL32(18,32,20,255),IM_COL32(28,50,32,255),IM_COL32(36,64,40,255),
            IM_COL32(130,190,140,255),IM_COL32(180,230,190,255),IM_COL32(70,120,80,255),
            IM_COL32(22,42,26,255),IM_COL32(32,56,36,255),IM_COL32(210,245,215,255),
            IM_COL32(140,210,150,255),IM_COL32(190,240,200,255),IM_COL32(220,250,225,255),
            IM_COL32(52,211,153,255),IM_COL32(110,231,183,255)};
}
inline Theme get() {
    switch (Vars.Settings.theme) {
        case 1: return Blue();
        case 2: return Red();
        case 3: return Green();
        default: return Dark();
    }
}
} // namespace pal

// ============================================================================
//  ANIMATION
// ============================================================================
namespace anim {
    inline float menuAlpha = 0.f;
    inline float menuOffY  = 0.f;
    inline void tick(float dt) {
        float tA = open ? 1.f : 0.f;
        menuAlpha += (tA - menuAlpha) * (1.f - expf(-dt * 14.f));
        float tO  = open ? 0.f : 20.f;
        menuOffY  += (tO - menuOffY)  * (1.f - expf(-dt * 14.f));
    }
    inline std::unordered_map<ImGuiID, float> g;
    inline float lerp(ImGuiID id, float target, float spd = 14.f) {
        auto it = g.find(id); if (it == g.end()) { g[id] = target; return target; }
        float& v = it->second;
        v += (target - v) * (1.f - expf(-spd * ImGui::GetIO().DeltaTime));
        if (fabsf(target - v) < 0.002f) v = target;
        return v;
    }
} // namespace anim

inline float  S(float v)           { return v * scale; }
inline ImVec2 S2(float x, float y) { return ImVec2(x*scale, y*scale); }

inline ImU32 Mix(ImU32 a, ImU32 b, float t) {
    ImVec4 A = ImGui::ColorConvertU32ToFloat4(a), B = ImGui::ColorConvertU32ToFloat4(b);
    return ImGui::ColorConvertFloat4ToU32(
        ImVec4(A.x+(B.x-A.x)*t, A.y+(B.y-A.y)*t, A.z+(B.z-A.z)*t, A.w+(B.w-A.w)*t));
}
inline ImU32 WithAlpha(ImU32 c, float a) {
    ImVec4 v = ImGui::ColorConvertU32ToFloat4(c); v.w *= a;
    return ImGui::ColorConvertFloat4ToU32(v);
}
static const float PI = 3.14159265f;

// ============================================================================
//  ICONS
// ============================================================================
enum Ico { I_COMBAT, I_VISUALS, I_MISC, I_SETTINGS, I_CLOSE, I_LEFT, I_RIGHT, I_CAMERA };

inline void DrawIcon(ImDrawList* d, int ico, ImVec2 c, float size, ImU32 col, float th) {
    const float k = size / 24.f;
    auto P = [&](float x, float y){ return ImVec2(c.x+(x-12.f)*k, c.y+(y-12.f)*k); };
    switch (ico) {
    case I_COMBAT:
        d->AddCircle(c, 7.5f*k, col, 32, th);
        d->AddCircle(c, 2.5f*k, col, 20, th);
        d->AddLine(P(12,1.5f),P(12,4.5f),col,th); d->AddLine(P(12,19.5f),P(12,22.5f),col,th);
        d->AddLine(P(1.5f,12),P(4.5f,12),col,th); d->AddLine(P(19.5f,12),P(22.5f,12),col,th);
        break;
    case I_VISUALS:
        d->PathLineTo(P(2,12)); d->PathBezierCubicCurveTo(P(7,5),P(17,5),P(22,12));
        d->PathBezierCubicCurveTo(P(17,19),P(7,19),P(2,12)); d->PathStroke(col,0,th);
        d->AddCircle(c,3.2f*k,col,20,th);
        break;
    case I_MISC:
        for(int i=0;i<4;i++){
            float x=(i%2)?13.5f:3.f, y=(i/2)?13.5f:3.f;
            d->AddRect(P(x,y),P(x+7.5f,y+7.5f),col,2.f*k,0,th);
        } break;
    case I_SETTINGS: {
        const int teeth=8;
        for(int i=0;i<teeth;i++){
            float a=i*2.f*PI/teeth;
            const float q[4][2]={{7.6f,a-0.30f},{10.f,a-0.17f},{10.f,a+0.17f},{7.6f,a+0.30f}};
            for(auto& p:q) d->PathLineTo(ImVec2(c.x+cosf(p[1])*p[0]*k, c.y+sinf(p[1])*p[0]*k));
        }
        d->PathStroke(col,ImDrawFlags_Closed,th);
        d->AddCircle(c,3.f*k,col,20,th);
        break;
    }
    case I_CLOSE: d->AddLine(P(6,6),P(18,18),col,th); d->AddLine(P(18,6),P(6,18),col,th); break;
    case I_LEFT:  d->PathLineTo(P(14.5f,5));d->PathLineTo(P(8,12));d->PathLineTo(P(14.5f,19));d->PathStroke(col,0,th); break;
    case I_RIGHT: d->PathLineTo(P(9.5f,5));d->PathLineTo(P(16,12));d->PathLineTo(P(9.5f,19));d->PathStroke(col,0,th); break;
    // FreeCam icon — видеокамера
    case I_CAMERA:
        d->AddRectFilled(P(2,8),P(16,18),col,1.5f*k);
        d->PathLineTo(P(16,9));d->PathLineTo(P(22,6));d->PathLineTo(P(22,18));d->PathLineTo(P(16,15));
        d->PathFillConvex(col);
        break;
    }
}

inline void DrawLogo(ImDrawList* d, ImVec2 c, float size) {
    const float k = size/30.f;
    auto P=[&](float x,float y){return ImVec2(c.x+(x-15.f)*k,c.y+(y-15.f)*k);};
    ImVec2 in[4]={P(18.4f,11.6f),P(18.4f,18.4f),P(11.6f,18.4f),P(11.6f,11.6f)};
    d->AddQuadFilled(in[0],in[1],in[2],in[3],IM_COL32_WHITE);
    d->AddTriangleFilled(in[3],P(15,2),in[0],IM_COL32_WHITE);
    d->AddTriangleFilled(in[0],P(28,15),in[1],IM_COL32_WHITE);
    d->AddTriangleFilled(in[1],P(15,28),in[2],IM_COL32_WHITE);
    d->AddTriangleFilled(in[2],P(2,15),in[3],IM_COL32_WHITE);
}

// ============================================================================
//  WIDGETS
// ============================================================================
inline void ToggleWidget(ImVec2 pos, bool* v, const pal::Theme& T) {
    ImDrawList* d = ImGui::GetWindowDrawList();
    ImVec2 sz = S2(44,23);
    ImGui::SetCursorScreenPos(pos);
    if (ImGui::InvisibleButton("##tg", sz)) *v = !*v;
    float t = anim::lerp(ImGui::GetID("##tg"), *v?1.f:0.f, 12.f);
    d->AddRectFilled(pos, pos+sz, Mix(T.TRACK, T.ACCENT, t), sz.y*0.5f);
    float r = S(8.5f);
    d->AddCircleFilled(ImVec2(pos.x+S(3)+r+t*S(21), pos.y+sz.y*0.5f), r, IM_COL32_WHITE, 24);
}

inline bool ArrowBtn(const char* id, ImVec2 pos, bool right, const pal::Theme& T) {
    ImDrawList* d = ImGui::GetWindowDrawList();
    ImVec2 sz = S2(23,23);
    ImGui::SetCursorScreenPos(pos);
    bool pressed = ImGui::InvisibleButton(id, sz);
    bool hov = ImGui::IsItemHovered(), act = ImGui::IsItemActive();
    float h = anim::lerp(ImGui::GetID(id), hov?1.f:0.f);
    float sc = act?0.9f:1.f;
    ImVec2 cc = pos+sz*0.5f, half=sz*0.5f*sc;
    d->AddRectFilled(cc-half, cc+half, Mix(T.TRACK,T.TRACK_H,h), S(7)*sc);
    DrawIcon(d, right?I_RIGHT:I_LEFT, cc, S(12)*sc, Mix(T.NAV,IM_COL32_WHITE,h), S(1.8f));
    return pressed;
}

inline void SelectorWidget(float right, float cy, int* idx, const char* const* opts, int n, const pal::Theme& T) {
    ImDrawList* d = ImGui::GetWindowDrawList();
    float top = cy-S(11.5f);
    if (ArrowBtn("##l", ImVec2(right-S(122),top), false, T)) *idx=(*idx-1+n)%n;
    if (ArrowBtn("##r", ImVec2(right-S(23), top), true,  T)) *idx=(*idx+1)%n;
    const char* txt = opts[*idx];
    ImVec2 ts = ImGui::CalcTextSize(txt);
    float cx2 = right-S(23)-S(6)-S(32);
    d->AddText(ImVec2(cx2-ts.x*0.5f, cy-ts.y*0.5f), T.VAL, txt);
}

inline void SliderWidgetF(float right, float cy, float* v, float mn, float mx,
                          const pal::Theme& T, const char* fmt="%.1f") {
    ImDrawList* d=ImGui::GetWindowDrawList();
    const float w=S(170),h=S(5),x0=right-S(210);
    ImGui::SetCursorScreenPos(ImVec2(x0-S(7),cy-S(9)));
    ImGui::InvisibleButton("##slf",ImVec2(w+S(14),S(18)));
    bool act=ImGui::IsItemActive();
    if(act){
        float p=(ImGui::GetIO().MousePos.x-x0)/w;
        if(p<0.f)p=0.f; if(p>1.f)p=1.f;
        *v=mn+p*(mx-mn);
    }
    float p=(*v-mn)/(mx-mn); if(p<0)p=0; if(p>1)p=1;
    float pa=anim::lerp(ImGui::GetID("##slfA"),p,24.f);
    ImVec2 a(x0,cy-h*0.5f);
    d->AddRectFilled(a,a+ImVec2(w,h),T.TRACK,h*0.5f);
    if(w*pa>1.f) d->AddRectFilled(a,ImVec2(x0+w*pa,a.y+h),T.ACCENT,h*0.5f);
    d->AddCircleFilled(ImVec2(x0+w*pa,cy),S(6.5f)*(act?1.15f:1.f),IM_COL32_WHITE,20);
    char buf[32]; snprintf(buf,sizeof buf,fmt,*v);
    ImVec2 ts=ImGui::CalcTextSize(buf);
    d->AddText(ImVec2(right-ts.x,cy-ts.y*0.5f),T.VAL,buf);
}

// RGB color strip (compact, 3 sliders R/G/B)
inline void ColorWidget(float right, float cy, float* r, float* g, float* b, const pal::Theme& T) {
    const float sw = S(50), sh = S(5);
    const float gap = S(4);
    float x0 = right - S(3)*(sw + gap);
    float* ch[3] = {r, g, b};
    ImU32 labels[3] = {IM_COL32(255,80,80,255), IM_COL32(80,200,80,255), IM_COL32(80,120,255,255)};
    ImDrawList* d = ImGui::GetWindowDrawList();
    for (int i = 0; i < 3; i++) {
        float x = x0 + i*(sw + gap);
        ImGui::SetCursorScreenPos(ImVec2(x, cy-S(8)));
        ImGui::InvisibleButton((i==0?"##cr":(i==1?"##cg":"##cb")), ImVec2(sw, S(16)));
        if (ImGui::IsItemActive()) {
            float p = (ImGui::GetIO().MousePos.x - x) / sw;
            if(p<0)p=0; if(p>1)p=1;
            *ch[i] = p;
        }
        float pa = anim::lerp(ImGui::GetID((i==0?"##crA":(i==1?"##cgA":"##cbA"))),*ch[i],24.f);
        d->AddRectFilled(ImVec2(x,cy-sh*0.5f), ImVec2(x+sw,cy+sh*0.5f), T.TRACK, sh*0.5f);
        if(sw*pa>1.f) d->AddRectFilled(ImVec2(x,cy-sh*0.5f), ImVec2(x+sw*pa,cy+sh*0.5f), labels[i], sh*0.5f);
        d->AddCircleFilled(ImVec2(x+sw*pa,cy),S(5.5f),IM_COL32_WHITE,16);
    }
}

// ============================================================================
//  ROW
// ============================================================================
enum RowType { R_TOGGLE, R_SELECT, R_SLIDER, R_SLIDERF, R_LABEL, R_COLOR3 };
struct Row {
    const char* label;
    RowType     type;
    bool*       b;
    int*        iv;
    float*      fv;
    float*      fv2; // для COLOR3: g
    float*      fv3; // для COLOR3: b
    const char* const* opts;
    int count, mn, mx;
    float fmn, fmx;
    const char* fmt;
};

inline Row T   (const char* l, bool* b)                                          { return {l,R_TOGGLE,b,nullptr,nullptr,nullptr,nullptr,nullptr,0,0,0,0,0,nullptr}; }
inline Row Sel (const char* l, int*v,const char*const*o,int n)                   { return {l,R_SELECT,nullptr,v,nullptr,nullptr,nullptr,o,n,0,0,0,0,nullptr}; }
inline Row SlF (const char* l, float*v,float mn,float mx,const char*f="%.1f")    { return {l,R_SLIDERF,nullptr,nullptr,v,nullptr,nullptr,nullptr,0,0,0,mn,mx,f}; }
inline Row Lbl (const char* l)                                                    { return {l,R_LABEL,nullptr,nullptr,nullptr,nullptr,nullptr,nullptr,0,0,0,0,0,nullptr}; }
inline Row Col3(const char* l, float* r, float* g, float* b)                     { return {l,R_COLOR3,nullptr,nullptr,r,g,b,nullptr,0,0,0,0,0,nullptr}; }

// ============================================================================
//  SECTIONS  (заменяем ALL на новое)
// ============================================================================
static const char* kThemeNames[] = { "Dark","Blue","Red","Green" };
static const char* kMaterial[]   = { "Flat","Glow","Wireframe" };
static const char* kBone[]       = { "Head","Neck","Chest","Pelvis","Arm","Leg" };
#define OPT(a) a,IM_ARRAYSIZE(a)

inline std::vector<struct Section>& Sections();

struct Tab     { const char* name; std::vector<Row> rows; };
struct Section { int icon; const char* name; std::vector<Tab> tabs; };

inline std::vector<Section>& Sections() {
    static std::vector<Section> s = {
        // ── Combat ──────────────────────────────────────────────────────────
        { I_COMBAT, "Combat", {
            { "AimBot", {
                T  ("AimBot Enable",    &Vars.Aimbot.enableAimbot),
                T  ("Scope Only",       &Vars.Aimbot.scopeCheck),
                T  ("On-screen Only",     &Vars.Aimbot.visibleOnly),
                T  ("Visible Check",      &Vars.Aimbot.visibleCheck),
                T  ("Draw FOV",         &Vars.Aimbot.drawfov),
                T  ("FOV Check",        &Vars.Aimbot.fovcheck),
                SlF("FOV radius (px)",  &Vars.Aimbot.fov,        30.f,400.f,"%.0f"),
                // Smooth: 0 = мгновенно/точно на голове, 100 = очень медленно
                SlF("Smooth (0=snap)",  &Vars.Aimbot.smooth,      0.f,100.f,"%.0f"),
                SlF("Max Distance (m)", &Vars.Aimbot.maxDistance, 50.f,800.f,"%.0f"),
                Sel("Target Bone",      &Vars.Aimbot.selectedBoneIndex, OPT(kBone)),
            }},
            { "SilentAim", {
                T  ("Origin Override (EXPERIMENT)", &Vars.Aimbot.silentAim),
                T  ("On-screen Only",     &Vars.Aimbot.silentVisibleOnly),
                T  ("Scope Only",       &Vars.Aimbot.silentScopeCheck),
                SlF("Silent FOV",       &Vars.Aimbot.silentFov,   30.f,400.f,"%.0f"),
                Sel("Target Bone",      &Vars.Aimbot.silentBoneIndex, OPT(kBone)),
            }},
            { "Gun", {
                T("No Recoil",   &Vars.Player.norecoil),
                T("No Spread",   &Vars.Player.nospread),
                T("Wallshot",    &Vars.Gun.wallshot),
                T("Inf Ammo",    &Vars.Gun.infinityammo),
            }},
        }},
        // ── Visuals ─────────────────────────────────────────────────────────
        { I_VISUALS, "Visuals", {
            { "ESP", {
                T("ESP Enable",          &Vars.ESP.enable),
                T("Box ESP",             &Vars.ESP.box),
                T("Player ID",           &Vars.ESP.name),
                T("Distance",            &Vars.ESP.distance),
                T("Health Bar (no source)",          &Vars.ESP.healthbar),
                T("Armor Bar (no source)",           &Vars.ESP.armorbar),
                T("Tracer",              &Vars.ESP.tracer),
                T("Off-Screen Arrows",   &Vars.ESP.outOfScreen),
                T("X-Ray Fill",          &Vars.ESP.xrayFill),
                T("Player Count HUD",    &Vars.ESP.count),
                SlF("Arrow Radius",      &Vars.ESP.arrowRadius, 60.f,400.f,"%.0f"),
            }},
            { "Style", {
                Col3("Box Color", &Vars.ESP.boxColor[0], &Vars.ESP.boxColor[1], &Vars.ESP.boxColor[2]),
                Col3("Line Color", &Vars.ESP.lineColor[0], &Vars.ESP.lineColor[1], &Vars.ESP.lineColor[2]),
                T("Black Outline", &Vars.ESP.outline),
                SlF("Line Thickness", &Vars.ESP.lineThickness, 1.f, 5.f, "%.1f"),
            }},
            { "Chams", {
                T  ("Enable Chams",     &Vars.Chams.enable),
                T  ("Include Screen-Filtered",    &Vars.Chams.throughWalls),
                Sel("Material",         &Vars.Chams.material, OPT(kMaterial)),
                SlF("Opacity",          &Vars.Chams.opacity,  0.05f,1.f,"%.2f"),
                Col3("Visible Color",   &Vars.Chams.r, &Vars.Chams.g, &Vars.Chams.b),
                Col3("Wall Color",      &Vars.Chams.wr, &Vars.Chams.wg, &Vars.Chams.wb),
            }},
            { "HitFX", {
                T("Effects Enable",  &HitEffect::enabled),
                T("Hit Effect",      &HitEffect::hitEnabled),
                T("Kill Effect",     &HitEffect::killEnabled),
            }},
        }},
        // ── Misc ────────────────────────────────────────────────────────────
        { I_MISC, "Misc", {
            { "Player", {
                T  ("Custom Cam FOV",   &Vars.Player.camerafov),
                SlF("Custom FOV",       &Vars.Player.customFov,    40.f,120.f,"%.0f"),
                T  ("FOV Arms",         &Vars.Player.armfov),
                SlF("Arms FOV",         &Vars.Player.armfovValue,  20.f,120.f,"%.0f"),
                T  ("Bunny Hop",        &Vars.Player.bunnyhope),
                SlF("Hop Speed",        &Vars.Player.hopevalue,    1.f,10.f,"%.1f"),
                T  ("Strafer",          &Vars.Player.strafer),
                T  ("Fast Knife",       &Vars.Player.fastknife),
                T  ("Air Jump",         &Vars.Player.airjump),
                T  ("Invisible",        &Vars.Player.invisible),
                T  ("Buy Anywhere",     &Vars.Player.buy_anywhere),
                T  ("Infinity Buy",     &Vars.Player.infinity_buy),
            }},
            { "World", {
                T("Sky Change",        &Vars.Sky.enable),
                T("Host Indicator",    &Vars.Host.enablehostindicator),
                T("Skip Phase",        &Vars.Host.skip),
                T("Cancel Match",      &Vars.Host.cancelmatch),
                T("Inf Grenades",      &Vars.grenade.infinitygrenades),
                T("Fast Detonation",   &Vars.grenade.fast_detonation),
                T("No Grenade Dmg",    &Vars.grenade.NoDamage),
            }},
        }},
        // ── FreeCam ─────────────────────────────────────────────────────────
        { I_CAMERA, "FreeCam", {
            { "FreeCam", {
                T  ("Enable FreeCam",  &Vars.FreeCam.enable),
                SlF("Flight Speed",    &Vars.FreeCam.speed, 1.f, 50.f, "%.1f"),
                Lbl("Internal camera / sticks + up-down"),
                Lbl("Left  = Move  /  Right = Look"),
            }},
        }},
        // ── Settings ────────────────────────────────────────────────────────
        { I_SETTINGS, "Settings", {
            { "Theme", {
                Sel("Menu Theme", &Vars.Settings.theme, OPT(kThemeNames)),
            }},
            { "Config", {
                SlF("ESP Corner Radius", &Vars.Settings.espround, 0.f,16.f,"%.1f"),
                SlF("External FOV (unused)", &Vars.Settings.camFov,   30.f,100.f,"%.1f"),
                T  ("Aspect Ratio",      &Vars.Player.aspect_ratio),
                SlF("Aspect Value",      &Vars.Player.aspect_value, 0.5f,2.5f,"%.2f"),
            }},
        }},
    };
    return s;
}

// ============================================================================
//  DRAW ROWS
// ============================================================================
inline bool Supported(const Row& r) {
    if (r.type==R_LABEL) return true;
    if (r.iv) return r.iv==&Vars.Settings.theme || r.iv==&Vars.Aimbot.selectedBoneIndex ||
        r.iv==&Vars.Aimbot.silentBoneIndex || r.iv==&Vars.Chams.material;
    if (r.b) return r.b==&Vars.ESP.enable || r.b==&Vars.ESP.box || r.b==&Vars.ESP.name ||
        r.b==&Vars.ESP.distance || r.b==&Vars.ESP.tracer || r.b==&Vars.ESP.xrayFill ||
        r.b==&Vars.ESP.count || r.b==&Vars.ESP.outline || r.b==&Vars.ESP.outOfScreen ||
        r.b==&Vars.Aimbot.enableAimbot || r.b==&Vars.Aimbot.scopeCheck || r.b==&Vars.Aimbot.visibleOnly ||
        r.b==&Vars.Aimbot.visibleCheck || r.b==&Vars.Aimbot.drawfov || r.b==&Vars.Aimbot.fovcheck || r.b==&Vars.Aimbot.silentAim ||
        r.b==&Vars.Aimbot.silentScopeCheck || r.b==&Vars.Aimbot.silentVisibleOnly ||
        r.b==&Vars.Player.norecoil || r.b==&Vars.Player.nospread || r.b==&Vars.Player.armfov ||
        r.b==&Vars.Player.camerafov || r.b==&Vars.Player.aspect_ratio || r.b==&Vars.FreeCam.enable ||
        r.b==&Vars.Chams.enable || r.b==&Vars.Chams.throughWalls;
    if (r.fv) return r.fv==&Vars.Settings.espround || r.fv==&Vars.ESP.lineThickness ||
        r.fv==&Vars.ESP.boxColor[0] || r.fv==&Vars.ESP.lineColor[0] || r.fv==&Vars.ESP.arrowRadius ||
        r.fv==&Vars.Aimbot.fov || r.fv==&Vars.Aimbot.smooth || r.fv==&Vars.Aimbot.maxDistance ||
        r.fv==&Vars.Aimbot.silentFov || r.fv==&Vars.Player.armfovValue || r.fv==&Vars.Player.customFov ||
        r.fv==&Vars.Player.aspect_value || r.fv==&Vars.FreeCam.speed || r.fv==&Vars.Chams.opacity ||
        r.fv==&Vars.Chams.r || r.fv==&Vars.Chams.wr;
    return false;
}

inline void DrawRows(std::vector<Row>& rows, const pal::Theme& T) {
    ImDrawList* d = ImGui::GetWindowDrawList();
    const float rowH = S(44);
    ImVec2 p = ImGui::GetCursorScreenPos();
    float  w = ImGui::GetContentRegionAvail().x;

    d->AddRectFilled(p, p+ImVec2(w, rowH*rows.size()), T.PANEL, S(9));

    for (size_t i = 0; i < rows.size(); i++) {
        Row& r = rows[i];
        ImVec2 rp(p.x, p.y+rowH*i);
        float cy    = rp.y + rowH*0.5f;
        float right = rp.x + w - S(17);
        float ctrlW = (r.type==R_TOGGLE)?S(44):
                      (r.type==R_SELECT)?S(122):
                      (r.type==R_SLIDERF||r.type==R_SLIDER)?S(210):
                      (r.type==R_COLOR3)?S(3*54+8):0.f;

        ImGui::PushID((int)i);

        // Метка
        d->PushClipRect(rp, ImVec2(right-ctrlW-S(12), rp.y+rowH), true);
        d->AddText(ImVec2(rp.x+S(15), cy-ImGui::GetFontSize()*0.5f), T.TXT, r.label);
        d->PopClipRect();

        // Разделитель
        if (i+1 < rows.size())
            d->AddLine(ImVec2(rp.x+S(10),rp.y+rowH-1), ImVec2(rp.x+w-S(10),rp.y+rowH-1),
                       WithAlpha(T.LINE,0.5f));

        if (!Supported(r)) {
            const char* pending = "NO BACKEND";
            const ImVec2 ts = ImGui::CalcTextSize(pending);
            d->AddText(ImVec2(right-ts.x, cy-ts.y*0.5f), T.DIM, pending);
            ImGui::PopID();
            continue;
        }
        switch (r.type) {
        case R_TOGGLE:  ToggleWidget(ImVec2(right-S(44), cy-S(11.5f)), r.b, T);       break;
        case R_SELECT:  SelectorWidget(right, cy, r.iv, r.opts, r.count, T);          break;
        case R_SLIDERF: SliderWidgetF(right, cy, r.fv, r.fmn, r.fmx, T, r.fmt);      break;
        case R_COLOR3:  ColorWidget(right, cy, r.fv, r.fv2, r.fv3, T);               break;
        case R_LABEL:
            // Отображаем как серый текст по центру
            { ImVec2 ts=ImGui::CalcTextSize(r.label);
              d->AddText(ImVec2(rp.x+(w-ts.x)*0.5f, cy-ts.y*0.5f), T.DIM, r.label); }
            break;
        default: break;
        }
        ImGui::PopID();
    }

    ImGui::SetCursorScreenPos(p);
    ImGui::Dummy(ImVec2(w, rowH*rows.size()));
}

// ============================================================================
//  STATE / NAV
// ============================================================================
inline int g_cur    = 1;
inline int g_tab[5] = {};

inline bool NavButton(const char* id, ImVec2 pos, ImVec2 sz, int ico,
                      bool active, const pal::Theme& T) {
    ImGui::SetCursorScreenPos(pos);
    bool pressed = ImGui::InvisibleButton(id, sz);
    bool hov = ImGui::IsItemHovered(), act2 = ImGui::IsItemActive();
    float h = anim::lerp(ImGui::GetID(id), hov?1.f:0.f);
    float a = anim::lerp(ImGui::GetID((std::string(id)+"a").c_str()), active?1.f:0.f);
    ImDrawList* d = ImGui::GetWindowDrawList();
    ImU32 col = Mix(Mix(T.NAV,T.NAV_H,h), T.ACCENT, a);
    float sc = act2?0.9f:1.f;
    DrawIcon(d, ico, pos+sz*0.5f, S(22)*sc, col, S(1.7f));
    if (a > 0.01f)
        d->AddRectFilled(pos+S2(3,12), pos+S2(5,32), WithAlpha(T.ACCENT,a), S(1));
    return pressed;
}

static bool s_exitPending = false;

// ============================================================================
//  RENDER
// ============================================================================
inline void Render() {
    static double lastT = 0.0;
    double now = ImGui::GetTime();
    float dt = (float)(now-lastT); if(dt>0.1f)dt=0.1f; lastT=now;

    anim::tick(dt);

    // HitEffect всегда
    ImDrawList* bgDl = ImGui::GetBackgroundDrawList();
    HitEffect::tick(bgDl, dt);

    if (anim::menuAlpha < 0.01f && !open) return;

    pal::Theme T = pal::get();
    auto& secs   = Sections();

    ImGuiIO& io = ImGui::GetIO();
    scale = std::max(0.1f, std::min({1.5f, (io.DisplaySize.x-24.f)/696.f, (io.DisplaySize.y-100.f)/448.f}));
    const ImVec2 size = S2(696, 448);
    float winX = io.DisplaySize.x*0.5f - size.x*0.5f;
    float winY = io.DisplaySize.y*0.5f - size.y*0.5f + anim::menuOffY;

    ImGui::SetNextWindowPos({winX,winY}, ImGuiCond_Always);
    ImGui::SetNextWindowSize(size, ImGuiCond_Always);
    ImGui::SetNextWindowBgAlpha(0.f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,   {0,0});
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.f);
    ImGui::Begin("##litvin_encore", nullptr,
        ImGuiWindowFlags_NoDecoration|ImGuiWindowFlags_NoMove|
        ImGuiWindowFlags_NoScrollbar|ImGuiWindowFlags_NoSavedSettings|
        ImGuiWindowFlags_NoScrollWithMouse|ImGuiWindowFlags_NoBackground);
    ImGui::PopStyleVar(2);

    ImDrawList* d = ImGui::GetWindowDrawList();
    const ImVec2 o = ImGui::GetWindowPos();

    // Тень
    ImDrawList* bg = ImGui::GetBackgroundDrawList();
    for (int i=1;i<=6;i++){
        float e=S(3.f*i);
        bg->AddRectFilled(o-ImVec2(e,e)+ImVec2(0,S(10)),
                          o+size+ImVec2(e,e)+ImVec2(0,S(10)),
                          WithAlpha(IM_COL32(0,0,0,12),(float)i/6.f*anim::menuAlpha),
                          S(18)+e);
    }

    // Фон
    d->AddRectFilled(o, o+size, WithAlpha(T.BG, anim::menuAlpha), S(18));
    d->AddRect(o, o+size, WithAlpha(T.LINE,anim::menuAlpha), S(18), 0, 1.f);

    // Sidebar (5 секций теперь)
    const ImVec2 sp = o+S2(9,9);
    d->AddRectFilled(sp, sp+S2(77,430), WithAlpha(T.SIDE,anim::menuAlpha), S(12));
    DrawLogo(d, sp+S2(38.5f,31), S(30));

    // FreeCam индикатор в sidebar
    if (Vars.FreeCam.enable) {
        ImU32 fcCol = Vars.FreeCam.found
            ? IM_COL32(80,230,80,220) : IM_COL32(230,80,80,180);
        d->AddCircleFilled(sp+S2(68,16), S(5), fcCol, 12);
    }

    static const float navY[5] = {79,138,197,256,315};
    for (int i=0;i<5;i++){
        ImVec2 bp = sp+S2(0,navY[i]);
        ImGui::PushID(i);
        if (NavButton("##nav",bp,S2(77,44),secs[i].icon,g_cur==i,T))
            g_cur = i;
        ImGui::PopID();
    }

    // Top bar — название секции
    {
        d->AddText(o+S2(105,20), WithAlpha(T.NAV_H, anim::menuAlpha), secs[g_cur].name);
    }

    // Close button
    {
        ImVec2 cbp = o+S2(653,9);
        ImGui::SetCursorScreenPos(cbp);
        bool cp = ImGui::InvisibleButton("##close", S2(35,35));
        bool ch = ImGui::IsItemHovered(), ca = ImGui::IsItemActive();
        float ch2 = anim::lerp(ImGui::GetID("##close"), ch?1.f:0.f);
        float csc = ca?0.9f:1.f;
        ImVec2 cc=cbp+S2(17.5f,17.5f), half=S2(17.5f,17.5f)*csc;
        d->AddRectFilled(cc-half,cc+half,Mix(T.BTN,T.BTN_H,ch2),S(9)*csc);
        DrawIcon(d,I_CLOSE,cc,S(19)*csc,WithAlpha(IM_COL32(168,170,176,255),anim::menuAlpha),S(2));
        if (cp) open=false;
    }

    // Вкладки текущей секции
    Section& sec = secs[g_cur];
    for (int i=0;i<(int)sec.tabs.size();i++){
        ImVec2 tp = o+S2(96+118.f*i,52), ts2=S2(112,35);
        ImGui::PushID(100+i);
        ImGui::SetCursorScreenPos(tp);
        if (ImGui::InvisibleButton("##tab",ts2)) g_tab[g_cur]=i;
        bool on  = g_tab[g_cur]==i;
        bool hov = ImGui::IsItemHovered(), act2=ImGui::IsItemActive();
        float aa = anim::lerp(ImGui::GetID("##ta"), on?1.f:0.f);
        float hh = anim::lerp(ImGui::GetID("##th"), (hov&&!on)?1.f:0.f);
        float sc2= act2?0.97f:1.f;
        ImVec2 cc2=tp+ts2*0.5f, half2=ts2*0.5f*sc2;
        d->AddRectFilled(cc2-half2,cc2+half2,
            WithAlpha(Mix(Mix(T.TAB,T.BTN,hh),T.BTN,aa), anim::menuAlpha), S(9));
        if (aa>0.01f)
            d->AddRectFilled(tp+S2(8,32), tp+S2(114,34),
                WithAlpha(T.ACCENT,aa*anim::menuAlpha), S(2));
        const char* nm = sec.tabs[i].name;
        ImVec2 tsz=ImGui::CalcTextSize(nm);
        d->AddText(cc2-tsz*0.5f,
            WithAlpha(on?T.NAV_H:Mix(T.TXT,T.TXT_H,hh), anim::menuAlpha), nm);
        ImGui::PopID();
    }

    // Контент
    ImGui::SetCursorScreenPos(o+S2(96,99));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,{0,0});
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing,{0,0});
    ImGui::PushStyleColor(ImGuiCol_ChildBg,IM_COL32(0,0,0,0));
    ImGui::BeginChild("##panel",S2(591,340),false,
        ImGuiWindowFlags_NoScrollbar|ImGuiWindowFlags_NoBackground);

    int tab = g_tab[g_cur];
    if (tab < (int)sec.tabs.size())
        DrawRows(sec.tabs[tab].rows, T);

    // EXIT кнопка (только Settings, последняя секция = 4)
    if (g_cur == 4) {
        float bw=S(200),bh=S(36);
        float bx=S(591)*0.5f-bw*0.5f, by=S(340)-bh-S(12);
        ImGui::SetCursorScreenPos(o+S2(96,0)+ImVec2(bx,by));
        bool ep=ImGui::InvisibleButton("##exit",ImVec2(bw,bh));
        bool eh=ImGui::IsItemHovered(), ea=ImGui::IsItemActive();
        float et=anim::lerp(ImGui::GetID("##exith"),eh?1.f:0.f);
        ImVec2 ebp=ImGui::GetItemRectMin();
        ImDrawList* pd=ImGui::GetWindowDrawList();
        pd->AddRectFilled(ebp,ebp+ImVec2(bw,bh),
            Mix(IM_COL32(60,20,20,200),IM_COL32(200,40,40,220),et),S(8));
        pd->AddRect(ebp,ebp+ImVec2(bw,bh),
            Mix(IM_COL32(120,40,40,200),IM_COL32(240,80,80,255),et),S(8),0,1.5f);
        const char* etxt=ea?"Pausing...":"PAUSE / HIDE";
        ImVec2 ets=ImGui::CalcTextSize(etxt);
        pd->AddText(ebp+ImVec2((bw-ets.x)*0.5f,(bh-ets.y)*0.5f),IM_COL32(255,200,200,255),etxt);
        if (ep){ open=false; s_exitPending=true; }
    }

    ImGui::EndChild();
    ImGui::PopStyleColor();
    ImGui::PopStyleVar(2);
    ImGui::End();
}

inline bool ShouldExit() {
    if (s_exitPending){ s_exitPending=false; return true; }
    return false;
}

} // namespace LitvinMenu
