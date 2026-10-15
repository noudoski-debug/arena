#ifndef ENCOREDLC_VARS_H
#define ENCOREDLC_VARS_H
#include <string>

struct MenuVars {
    bool openmenu = false;
    bool watera   = true;
    bool darkTheme = true;
    float scrollsize = 8.0f;
    float tab_alpha  = 1.0f;
    float tab_add    = 0.0f;
};

struct AimbotVars {
    // ── Обычный Aim ──────────────────────────────────────────────────────────
    bool  enableAimbot      = false;
    bool  scopeCheck        = true;
    int   aimBind           = 273;
    // smooth: 0 = мгновенно/максимально быстро, 100 = очень плавно/медленно
    float smooth            = 45.0f;
    bool  fovcheck          = true;
    float fov               = 150.0f;
    int   selectedBoneIndex = 0;
    float headDrop          = 0.0f;
    const char* boneNames[6] = {"Head","Neck","Chest","Pelvis","Arm","Leg"};
    bool  drawfov           = false;
    float maxDistance       = 300.0f;
    bool  deathCheck        = true;
    bool  visibleOnly       = false;   // аим только по видимым врагам

    // ── Silent Aim (scoped ray redirect, experimental) ───────────────────────────────────────────
    bool  silentAim         = false;
    float silentFov         = 120.0f;
    int   silentBoneIndex   = 0;       // точка experimental ray redirect: head/neck/...
    bool  silentDeathCheck  = true;
    bool  silentVisibleOnly = false;   // silent только по видимым
    bool  silentScopeCheck  = false;   // silent только в прицеле

    // устаревшие — оставляем для совместимости с cfg, но не используем в логике
    bool  smooth_enabled    = true;
    float smooth_factor     = 4.0f;
    float aimSpeed          = 16.0f;
    float aimYSpeed         = 10.0f;
    bool  noSpread          = false;
    bool  noRecoil          = false;
};

struct GunVars {
    bool infinityammo = false;
    bool wallshot     = false;
    bool norecoil     = false;
};

struct EspCustomVars {
    bool healthtext   = false;
    bool armortext    = false;
    bool ammotext     = false;
    int  lineselect   = 0;
    const char* linepos[3] = {"Top","Center","Bottom"};
    bool hpGlow       = false;
    bool armorGlow    = false;
    bool ammoGlow     = false;
    bool gradientbars = false;
    bool hpGradient   = false;
    bool armorGradient= false;
    bool ammoGradient = false;
};

struct ESPVars {
    float boxColor[4] = {1, 1, 1, 1};
    float lineColor[4] = {1, 1, 1, 1};
    float lineThickness = 2.f;
    bool outline = false;
    bool  enable      = false;
    bool  box         = true;
    bool  tracer      = false;
    bool  name        = true;
    bool  distance    = true;
    bool  count       = true;
    bool  xrayFill    = true;
    bool  healthbar   = false;
    bool  armorbar    = false;
    bool  ammobar     = false;
    bool  outOfScreen = false;
    float arrowRadius = 140.0f;
    EspCustomVars EspCustom;
};

struct ChamsVars {
    bool  enable       = false;
    bool  throughWalls = false;
    int   material     = 0;
    float opacity      = 0.75f;
    float r = 0.39f, g = 0.71f, b = 1.0f;
    float wr = 1.0f, wg = 0.2f, wb = 0.2f;
};

struct PlayerVars {
    bool  invisible    = false;
    bool  streamer     = false;
    bool  camerafov    = false;
    float customFov    = 70.0f;
    bool  armfov       = false;
    float armfovValue  = 60.0f;
    bool  norecoil     = false;
    bool  nospread     = false;
    bool  aspect_ratio = false;
    float aspect_value = 1.0f;
    bool  bunnyhope    = false;
    float hopevalue    = 2.0f;
    bool  strafer      = false;
    bool  buy_anywhere = false;
    bool  infinity_buy = false;
    bool  fastknife    = false;
    bool  airjump      = false;
    bool  viewmodel    = false;
    float view_x = 0, view_y = 0, view_z = 0;
    bool  setscore = false, setkills = false;
    bool  setassist = false, setdeath = false;
    bool  clumsy = false, scalehead = false;
};

struct SkyVars     { bool enable = false; };
struct HostVars    { bool enablehostindicator=false; bool skip=false; bool cancelmatch=false; };
struct GrenadeVars { bool infinitygrenades=false; bool fast_detonation=false; bool NoDamage=false; };

struct SettingsVars {
    float espround = 4.0f;
    float camFov   = 53.0f;
    int   theme    = 0;
};

struct FreeCamVars {
    bool  enable = false;
    float speed  = 5.0f;
    bool  found  = false;
    float posX = 0.f, posY = 0.f, posZ = 0.f;
    float yaw = 0.f, pitch = 0.f;
};

struct AllVars {
    MenuVars     Menu;
    AimbotVars   Aimbot;
    GunVars      Gun;
    ESPVars      ESP;
    ChamsVars    Chams;
    PlayerVars   Player;
    SkyVars      Sky;
    HostVars     Host;
    GrenadeVars  grenade;
    SettingsVars Settings;
    FreeCamVars  FreeCam;
};

extern AllVars Vars;

#endif // ENCOREDLC_VARS_H
