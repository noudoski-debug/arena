#include "Runtime.h"
#include "GameOffsets.h"
#include "dobby.h"
#include <android/log.h>
#include <link.h>
#include <atomic>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <mutex>
#include <unordered_map>
#include <unistd.h>

namespace runtime {
namespace {
using Clock = std::chrono::steady_clock;
using modules::Vec3;
using modules::Vec2;
using modules::Quaternion;
using Method = const void*; // IL2CPP's hidden MethodInfo argument.
using Lifecycle = void (*)(void*, Method);
struct Api {
    Vec3 (*project)(void*, Vec3, Method) = nullptr;
    Vec3 (*position)(void*, Method) = nullptr;
    void* (*transform)(void*, Method) = nullptr;
    void* (*camera)(Method) = nullptr;
    int (*width)(Method) = nullptr;
    int (*height)(Method) = nullptr;
    bool (*local)(void*, Method) = nullptr;
    Lifecycle update = nullptr, disable = nullptr, late = nullptr, weaponUpdate = nullptr, fpLate = nullptr;
    void (*setPosition)(void*, Vec3, Method) = nullptr;
    Quaternion (*rotation)(void*, Method) = nullptr;
    void (*setRotation)(void*, Quaternion, Method) = nullptr;
    Vec3 (*forward)(void*, Method) = nullptr;
    Vec3 (*right)(void*, Method) = nullptr;
    float (*fov)(void*, Method) = nullptr;
    void (*setFov)(void*, float, Method) = nullptr;
    float (*aspect)(void*, Method) = nullptr;
    void (*setAspect)(void*, float, Method) = nullptr;
};
struct TimedBox { Box box; Clock::time_point time; Vec3 feet, head, right; };
struct CameraState {
    void* camera=nullptr; void* transform=nullptr;
    bool free=false, fov=false, aspect=false;
    Vec3 originalPosition, position; Quaternion originalRotation;
    float originalFov=0, originalAspect=0, yaw=0, pitch=0;
};
struct State {
    Api api;
    std::mutex optionsMutex;
    AllVars options;
    modules::Controls controls;
    std::atomic<bool> moduleReady{false};
    void* localPlayer=nullptr;
    CameraState camera;
    bool silentSaved=false;
    Vec3 originalTick;
    Clock::time_point lastAim{}, lastCamera{};
    bool publishedFreecam=false;
    Vec3 publishedCamera;
    std::atomic<bool> enabled{false}, ready{false};
    std::atomic<const char*> message{"Waiting for libil2cpp.so"};
    std::mutex mutex;
    std::unordered_map<uintptr_t, TimedBox> players;
};
State& state() { static State s; return s; }

template<class T> T field(void* object, uintptr_t offset) {
    T value{};
    std::memcpy(&value, static_cast<char*>(object) + offset, sizeof(value));
    return value;
}
std::string userId(void* player) {
    void* text = field<void*>(player, offsets::UserId);
    if (!text) return {};
    const int length = field<int>(text, 0x10); // IL2CPP ARM64 System.String layout.
    if (length < 1 || length > 64) return {};
    std::string result;
    for (int i = 0; i < length; ++i) {
        uint32_t c = field<uint16_t>(text, 0x14 + i * 2);
        if (c >= 0xD800 && c <= 0xDBFF && i + 1 < length) {
            uint32_t lo = field<uint16_t>(text, 0x14 + (i + 1) * 2);
            if (lo >= 0xDC00 && lo <= 0xDFFF) { c = 0x10000 + ((c-0xD800)<<10) + lo-0xDC00; ++i; }
        }
        if (c >= 0xD800 && c <= 0xDFFF) c = 0xFFFD;
        if (c < 0x80) result += static_cast<char>(c);
        else if (c < 0x800) { result += char(0xC0 | (c>>6)); result += char(0x80 | (c&63)); }
        else if (c < 0x10000) { result += char(0xE0 | (c>>12)); result += char(0x80 | ((c>>6)&63)); result += char(0x80 | (c&63)); }
        else { result += char(0xF0 | (c>>18)); result += char(0x80 | ((c>>12)&63)); result += char(0x80 | ((c>>6)&63)); result += char(0x80 | (c&63)); }
    }
    return result;
}
void erase(void* player) {
    auto& s = state();
    std::lock_guard<std::mutex> lock(s.mutex);
    s.players.erase(reinterpret_cast<uintptr_t>(player));
}
void restoreSilent(void* player) {
    auto& s=state();
    if (s.silentSaved && player==s.localPlayer) {
        std::memcpy(static_cast<char*>(player)+offsets::LastTickPosition,&s.originalTick,sizeof(Vec3));
        s.silentSaved=false;
    }
}
void restoreCamera() {
    auto& s=state(); auto& c=s.camera;
    // Only touch the currently live camera, never a retained old-scene pointer.
    if (c.camera && s.api.camera && s.api.camera(nullptr)==c.camera) {
        if (c.free && s.api.transform(c.camera,nullptr)==c.transform) {
            s.api.setPosition(c.transform,c.originalPosition,nullptr);
            s.api.setRotation(c.transform,c.originalRotation,nullptr);
        }
        if (c.fov) s.api.setFov(c.camera,c.originalFov,nullptr);
        if (c.aspect) s.api.setAspect(c.camera,c.originalAspect,nullptr);
    }
    c={};
    std::lock_guard<std::mutex> lock(s.mutex); s.publishedFreecam=false;
}
void onDisable(void* player, Method method) {
    auto& s=state();
    if (player==s.localPlayer) { restoreSilent(player); restoreCamera(); s.localPlayer=nullptr; }
    erase(player); // Before the original callback can release references.
    if (state().api.disable) state().api.disable(player, method);
}
void applyLocal(void* player);
void onUpdate(void* player, Method method) {
    auto& s = state();
    restoreSilent(player); // Always, including disable/menu/no-target transitions.
    if (s.api.update) s.api.update(player, method);
    if (!player || !s.ready.load(std::memory_order_acquire)) return;
    if (s.api.local(player,nullptr)) {
        if(s.localPlayer!=player) {
            // Never restore a previous avatar's pending override into a new avatar.
            s.silentSaved=false;
            restoreCamera();
            s.localPlayer=player;
        }
        if (s.moduleReady.load()) applyLocal(player);
        erase(player); return;
    }
    if (!s.enabled.load()) return;
    // Unity calls stay on the game's Update thread. Render never dereferences a player.
    if (field<uint8_t>(player, offsets::Respawning) ||
        field<uint8_t>(player, offsets::Kicked)) { erase(player); return; }
    void* camera = s.api.camera(nullptr);
    void* transform = s.api.transform(player, nullptr);
    const int width = s.api.width(nullptr), height = s.api.height(nullptr);
    if (!camera || !transform || width <= 0 || height <= 0) { erase(player); return; }
    Vec3 feet = s.api.position(transform, nullptr);
    Vec3 head{feet.x, feet.y + 1.6f, feet.z};
    void* headTransform=field<void*>(player,offsets::WorldCameraRoot);
    if (headTransform) {
        Vec3 candidate=s.api.position(headTransform,nullptr);
        float distance=modules::length(candidate-feet);
        if (std::isfinite(distance) && distance>.5f && distance<3.5f) head=candidate;
    }
    Vec3 right=s.api.right?s.api.right(transform,nullptr):Vec3{1,0,0};
    const Vec3 fp = s.api.project(camera, feet, nullptr);
    const Vec3 hp = s.api.project(camera, head, nullptr);
    Box box;
    box.id = reinterpret_cast<uintptr_t>(player);
    box.label = userId(player);
    box.projected = fp.z > 0.1f && hp.z > 0.1f && std::isfinite(fp.x) &&
                    std::isfinite(fp.y) && std::isfinite(hp.x) && std::isfinite(hp.y);
    box.depth=hp.z;
    box.onScreen=box.projected && hp.x>=0 && hp.x<=width && hp.y>=0 && hp.y<=height;
    box.feetX = fp.x / width; box.feetY = 1.f - fp.y / height;
    box.headX = hp.x / width; box.headY = 1.f - hp.y / height;
    void* cameraTransform = s.api.transform(camera, nullptr);
    if (cameraTransform) {
        const Vec3 c = s.api.position(cameraTransform, nullptr);
        box.distance = std::sqrt((c.x-feet.x)*(c.x-feet.x)+(c.y-feet.y)*(c.y-feet.y)+(c.z-feet.z)*(c.z-feet.z));
    }
    std::lock_guard<std::mutex> lock(s.mutex);
    if (s.players.size() < 512 || s.players.count(box.id))
        s.players[box.id] = {std::move(box), Clock::now(), feet, head, right};
}
struct Options { AllVars values; modules::Controls controls; };
Options options() {
    auto& s=state(); std::lock_guard<std::mutex> lock(s.optionsMutex); return {s.options,s.controls};
}
template<class T> T* address(void* object, uintptr_t offset) {
    return object ? reinterpret_cast<T*>(static_cast<char*>(object)+offset) : nullptr;
}
float delta(Clock::time_point& last) {
    const auto now=Clock::now();
    float dt=std::chrono::duration<float>(now-last).count(); last=now;
    return dt>.001f && dt<.1f ? dt : 1.f/60.f;
}
bool selectTarget(void* camera, int bone, float fov, float maxDistance, bool onScreen, Vec3& result) {
    auto& s=state();
    void* transform=s.api.transform(camera,nullptr);
    if (!transform) return false;
    const Vec3 origin=s.api.position(transform,nullptr);
    const int width=s.api.width(nullptr),height=s.api.height(nullptr);
    if (width<=0 || height<=0) return false;
    float best=fov*fov;
    bool found=false;
    std::vector<TimedBox> targets;
    { std::lock_guard<std::mutex> lock(s.mutex); for (const auto& e:s.players) targets.push_back(e.second); }
    const auto cutoff=Clock::now()-std::chrono::milliseconds(300);
    for (const auto& t:targets) {
        if (t.time<cutoff || t.box.id==reinterpret_cast<uintptr_t>(s.localPlayer)) continue;
        Vec3 point=modules::bone(t.feet,t.head,t.right,bone);
        float distance=modules::length(point-origin);
        if (!std::isfinite(distance) || distance<.3f || distance>maxDistance) continue;
        Vec3 projected=s.api.project(camera,point,nullptr);
        if (projected.z<=.1f || !std::isfinite(projected.x) || !std::isfinite(projected.y)) continue;
        if (onScreen && (projected.x<0 || projected.y<0 || projected.x>width || projected.y>height)) continue;
        float x=projected.x-width*.5f, y=projected.y-height*.5f, metric=x*x+y*y;
        if (metric<best) { best=metric;result=point;found=true; }
    }
    return found;
}
void applyLocal(void* player) {
    auto& s=state(); const auto opts=options(); const auto& v=opts.values;
    const float dt=delta(s.lastAim);
    if (field<uint8_t>(player,offsets::Respawning) || field<uint8_t>(player,offsets::Kicked)) {
        restoreCamera(); return;
    }
    if (v.Menu.openmenu || v.FreeCam.enable) return;
    void* camera=s.api.camera(nullptr);
    if (!camera) return;
    void* fp=field<void*>(player,offsets::FpManager);
    float scope=fp?field<float>(fp,offsets::ScopeBlend):0.f;
    const bool scoped=std::isfinite(scope) && scope>.5f;
    if (v.Aimbot.enableAimbot && (!v.Aimbot.scopeCheck || scoped) &&
        (v.Aimbot.aimBind==0 || v.Aimbot.aimBind==273 || opts.controls.aimHeld)) {
        Vec3 target;
        if (selectTarget(camera,v.Aimbot.selectedBoneIndex,v.Aimbot.fovcheck?v.Aimbot.fov:100000.f,
                         v.Aimbot.maxDistance,v.Aimbot.visibleOnly,target)) {
            void* look=field<void*>(player,offsets::MouseLook);
            void* cameraTransform=s.api.transform(camera,nullptr);
            if (look && cameraTransform) {
                Vec2 original=field<Vec2>(look,offsets::MouseAngles); // x=pitch, y=yaw
                float pitch=field<float>(player,offsets::LookAngle);
                if (std::isfinite(original.x) && std::isfinite(original.y) && std::isfinite(pitch)) {
                    Vec2 desired=modules::angles(target-s.api.position(cameraTransform,nullptr));
                    Vec2 next=modules::aimStep({original.y,pitch},desired,v.Aimbot.smooth,dt);
                    *address<Vec2>(look,offsets::MouseAngles)={next.y,next.x};
                    *address<float>(player,offsets::LookAngle)=next.y;
                }
            }
        }
    }
    // Faithful port of Encore's experimental origin override, NOT a proven
    // bullet-redirection method. It changes lastTickPosition, never Transform.
    if (v.Aimbot.silentAim && (!v.Aimbot.silentScopeCheck || scoped)) {
        Vec3 target;
        if (selectTarget(camera,v.Aimbot.silentBoneIndex,v.Aimbot.silentFov,v.Aimbot.maxDistance,
                         v.Aimbot.silentVisibleOnly,target)) {
            s.originalTick=field<Vec3>(player,offsets::LastTickPosition);
            s.silentSaved=true;
            *address<Vec3>(player,offsets::LastTickPosition)=target;
        }
    }
}
void onLate(void* player,Method method) {
    auto& s=state();
    if (s.api.late) s.api.late(player,method);
    if (!s.moduleReady.load() || player!=s.localPlayer) return;
    const auto opts=options(); const auto& v=opts.values; auto& c=s.camera;
    const float dt=delta(s.lastCamera);
    void* camera=s.api.camera(nullptr);
    if (!camera) { c={}; std::lock_guard<std::mutex> lock(s.mutex); s.publishedFreecam=false; return; }
    if (c.camera!=camera) { c={}; c.camera=camera; }
    if (field<uint8_t>(player,offsets::Respawning) || field<uint8_t>(player,offsets::Kicked)) { restoreCamera(); return; }
    void* transform=s.api.transform(camera,nullptr);
    // Do not move the player's root when a nonstandard camera shares it.
    const bool free=v.FreeCam.enable && transform && transform!=s.api.transform(player,nullptr);
    if (free) {
        if (!c.free || transform!=c.transform) {
            c.originalPosition=c.position=s.api.position(transform,nullptr);
            c.originalRotation=s.api.rotation(transform,nullptr);
            Vec2 view=modules::angles(s.api.forward(transform,nullptr)); c.yaw=view.x;c.pitch=view.y;
            c.transform=transform;c.free=true;
        }
        if (!v.Menu.openmenu) {
            c.yaw+=opts.controls.lookX*80.f*dt;
            c.pitch=std::clamp(c.pitch+opts.controls.lookY*80.f*dt,-89.f,89.f);
            const float rad=c.yaw*.01745329252f;
            Vec3 forward{std::sin(rad),0,std::cos(rad)},right{std::cos(rad),0,-std::sin(rad)};
            c.position=c.position+(forward*(-opts.controls.moveY)+right*opts.controls.moveX+
                                   Vec3{0,opts.controls.vertical,0})*(std::clamp(v.FreeCam.speed,1.f,50.f)*dt);
        }
        s.api.setPosition(transform,c.position,nullptr);
        s.api.setRotation(transform,modules::rotation(c.yaw,c.pitch),nullptr);
    } else if (c.free) {
        if (transform==c.transform) {
            s.api.setPosition(transform,c.originalPosition,nullptr);
            s.api.setRotation(transform,c.originalRotation,nullptr);
        }
        c.free=false;
    }
    if (v.Player.camerafov) {
        if (!c.fov) { c.originalFov=s.api.fov(camera,nullptr); c.fov=true; }
        s.api.setFov(camera,std::clamp(v.Player.customFov,40.f,120.f),nullptr);
    } else if (c.fov) { s.api.setFov(camera,c.originalFov,nullptr);c.fov=false; }
    if (v.Player.aspect_ratio) {
        if (!c.aspect) { c.originalAspect=s.api.aspect(camera,nullptr);c.aspect=true; }
        s.api.setAspect(camera,std::clamp(v.Player.aspect_value,.5f,2.5f),nullptr);
    } else if (c.aspect) { s.api.setAspect(camera,c.originalAspect,nullptr);c.aspect=false; }
    { std::lock_guard<std::mutex> lock(s.mutex);s.publishedFreecam=c.free;s.publishedCamera=c.position; }
}
void onWeaponUpdate(void* weapon,Method method) {
    auto& s=state(); const auto opts=options();
    if (!s.moduleReady.load() || !weapon || !s.localPlayer ||
        field<void*>(weapon,offsets::WeaponPlayer)!=s.localPlayer) {
        if (s.api.weaponUpdate) s.api.weaponUpdate(weapon,method);
        return;
    }
    // Exact FPHitscan callback: no guessed native/managed type chain.
    void* config=field<void*>(weapon,offsets::WeaponConfig);
    void* recoil=config?field<void*>(config,offsets::RecoilConfig):nullptr;
    void* spread=config?field<void*>(config,offsets::DispersionConfig):nullptr;
    const bool nr=recoil && opts.values.Player.norecoil;
    const bool ns=spread && opts.values.Player.nospread;
    modules::ScopedValue<Vec2> minimum(address<Vec2>(recoil,offsets::MinRecoil),{},nr);
    modules::ScopedValue<Vec2> maximum(address<Vec2>(recoil,offsets::MaxRecoil),{},nr);
    modules::ScopedValue<float> a(address<float>(spread,offsets::MinFireDispersion),0,ns);
    modules::ScopedValue<float> b(address<float>(spread,offsets::MaxFireDispersion),0,ns);
    modules::ScopedValue<float> c(address<float>(spread,offsets::MinDispersion),0,ns);
    modules::ScopedValue<float> d(address<float>(spread,offsets::MaxDispersion),0,ns);
    modules::ScopedValue<float> e(address<float>(spread,offsets::DispersionIncrease),0,ns);
    if (s.api.weaponUpdate) s.api.weaponUpdate(weapon,method);
    // Every overridden field restored, even on early return/exception.
}
void onFpLate(void* fp,Method method) {
    auto& s=state();const auto opts=options();
    void* weapon=nullptr;
    if (s.moduleReady.load() && s.localPlayer && field<void*>(s.localPlayer,offsets::FpManager)==fp)
        weapon=field<void*>(fp,offsets::CurrentWeapon);
    const bool enabled=weapon && opts.values.Player.armfov;
    const int value=static_cast<int>(std::clamp(opts.values.Player.armfovValue,20.f,120.f));
    modules::ScopedValue<int> normal(address<int>(weapon,offsets::NormalFov),value,enabled);
    modules::ScopedValue<int> aim(address<int>(weapon,offsets::AimFov),value,enabled);
    if (s.api.fpLate) s.api.fpLate(fp,method);
}

struct Lookup { uintptr_t rva, address = 0; };
int findAddress(dl_phdr_info* info, size_t, void* arg) {
    const char* filename = std::strrchr(info->dlpi_name, '/');
    filename = filename ? filename + 1 : info->dlpi_name;
    if (std::strcmp(filename, "libil2cpp.so") != 0) return 0;
    auto& lookup = *static_cast<Lookup*>(arg);
    for (int i = 0; i < info->dlpi_phnum; ++i) {
        const auto& p = info->dlpi_phdr[i];
        if (p.p_type == PT_LOAD && (p.p_flags & PF_X) && lookup.rva >= p.p_vaddr &&
            lookup.rva - p.p_vaddr < p.p_memsz) {
            lookup.address = info->dlpi_addr + lookup.rva;
            return 1;
        }
    }
    return 0;
}
uintptr_t resolve(uintptr_t rva) { Lookup l{rva}; dl_iterate_phdr(findAddress, &l); return l.address; }
}

Frame snapshot() {
    auto& s = state();
    std::lock_guard<std::mutex> lock(s.mutex);
    Frame frame;
    frame.freecam=s.publishedFreecam;frame.cameraPosition=s.publishedCamera;
    const auto cutoff = Clock::now() - std::chrono::milliseconds(300);
    for (auto it = s.players.begin(); it != s.players.end();) {
        if (it->second.time < cutoff) it = s.players.erase(it);
        else { frame.boxes.push_back(it->second.box); ++frame.players; ++it; }
    }
    return frame;
}
void submit(const AllVars& options, modules::Controls controls) {
    auto& s=state();
    { std::lock_guard<std::mutex> lock(s.optionsMutex); s.options=options;s.controls=controls; }
    setEnabled(options.ESP.enable || options.Chams.enable || options.Aimbot.enableAimbot || options.Aimbot.silentAim);
}
void setEnabled(bool enabled) {
    auto& s = state();
    s.enabled.store(enabled);
    if (!enabled) { std::lock_guard<std::mutex> lock(s.mutex); s.players.clear(); }
}
const char* status() { return state().message.load(); }
void installGameHooks() {
    auto& s = state();
    uintptr_t update = 0;
    for (int i = 0; i < 120 && !update; ++i) {
        update = resolve(offsets::PlayerUpdate);
        if (!update) sleep(1);
    }
    const uintptr_t disable = resolve(offsets::PlayerOnDisable);
    s.api.project = reinterpret_cast<decltype(s.api.project)>(resolve(offsets::WorldToScreenPoint));
    s.api.position = reinterpret_cast<decltype(s.api.position)>(resolve(offsets::GetPosition));
    s.api.transform = reinterpret_cast<decltype(s.api.transform)>(resolve(offsets::GetTransform));
    s.api.camera = reinterpret_cast<decltype(s.api.camera)>(resolve(offsets::GetMainCamera));
    s.api.local = reinterpret_cast<decltype(s.api.local)>(resolve(offsets::IsLocalPlayer));
    s.api.width = reinterpret_cast<decltype(s.api.width)>(resolve(offsets::GetScreenWidth));
    s.api.height = reinterpret_cast<decltype(s.api.height)>(resolve(offsets::GetScreenHeight));
    s.api.setPosition=reinterpret_cast<decltype(s.api.setPosition)>(resolve(offsets::SetPosition));
    s.api.rotation=reinterpret_cast<decltype(s.api.rotation)>(resolve(offsets::GetRotation));
    s.api.setRotation=reinterpret_cast<decltype(s.api.setRotation)>(resolve(offsets::SetRotation));
    s.api.forward=reinterpret_cast<decltype(s.api.forward)>(resolve(offsets::GetForward));
    s.api.right=reinterpret_cast<decltype(s.api.right)>(resolve(offsets::GetRight));
    s.api.fov=reinterpret_cast<decltype(s.api.fov)>(resolve(offsets::GetFov));
    s.api.setFov=reinterpret_cast<decltype(s.api.setFov)>(resolve(offsets::SetFov));
    s.api.aspect=reinterpret_cast<decltype(s.api.aspect)>(resolve(offsets::GetAspect));
    s.api.setAspect=reinterpret_cast<decltype(s.api.setAspect)>(resolve(offsets::SetAspect));
    if (!update || !disable || !s.api.project || !s.api.position || !s.api.transform ||
        !s.api.camera || !s.api.local || !s.api.width || !s.api.height) {
        s.message.store("ERROR: IL2CPP RVAs unavailable; check exact game build");
    } else if (DobbyHook(reinterpret_cast<void*>(disable), reinterpret_cast<void*>(onDisable),
                         reinterpret_cast<void**>(&s.api.disable)) != 0) {
        s.message.store("ERROR: OnDisable hook failed");
    } else if (DobbyHook(reinterpret_cast<void*>(update), reinterpret_cast<void*>(onUpdate),
                         reinterpret_cast<void**>(&s.api.update)) != 0) {
        s.message.store("ERROR: Update hook failed");
    } else {
        s.ready.store(true, std::memory_order_release);
        s.message.store("Internal hooks ready / enable ESP in Visuals");
    }
    if (s.ready.load() && s.api.setPosition && s.api.rotation && s.api.setRotation &&
        s.api.forward && s.api.right && s.api.fov && s.api.setFov && s.api.aspect && s.api.setAspect) {
        bool ok=true;
        auto hook=[&](uintptr_t rva, Lifecycle callback, Lifecycle& original) {
            const uintptr_t target=resolve(rva);
            if (!target || DobbyHook(reinterpret_cast<void*>(target),reinterpret_cast<void*>(callback),
                                     reinterpret_cast<void**>(&original))!=0) ok=false;
        };
        hook(offsets::PlayerLateUpdate,onLate,s.api.late);
        hook(offsets::WeaponUpdate,onWeaponUpdate,s.api.weaponUpdate);
        hook(offsets::FpLateUpdate,onFpLate,s.api.fpLate);
        s.moduleReady.store(ok);
        s.message.store(ok?"Internal ESP + module hooks ready":"ESP ready / ERROR: gameplay module hooks failed");
    } else if (s.ready.load()) s.message.store("ESP ready / ERROR: module API unavailable");
    __android_log_print(ANDROID_LOG_INFO, "DearVision", "%s", status());
}
}
