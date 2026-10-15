#include "Runtime.h"
#include "GameOffsets.h"
#include "dobby.h"
#include <android/log.h>
#include <link.h>
#include <dlfcn.h>
#include <cstddef>
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
// Unity 1.14.11949 value-type layouts. Never use a managed object header here.
struct Ray { Vec3 origin, direction; };
struct RaycastHit { Vec3 point, normal; uint32_t face=0; float distance=0; Vec2 uv; int32_t collider=0; };
static_assert(sizeof(RaycastHit)==44 && offsetof(RaycastHit,collider)==0x28);
using RaycastInjected=bool (*)(const void*,const Ray*,float,RaycastHit*,int,int,Method);
struct Api {
    bool (*raycast)(Vec3,Vec3,RaycastHit*,float,int,int,Method)=nullptr;
    void* (*hitTransform)(RaycastHit*,Method)=nullptr;
    bool (*isChildOf)(void*,void*,Method)=nullptr;
    RaycastInjected raycastOriginal=nullptr;
    bool (*health)(void*,float&)=nullptr;
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
struct TimedBox {
    Box box; Clock::time_point time, labelTime{}; Vec3 feet, head, right;
    bool dead=false, sleeping=false, healthKnown=false;
    float health=0;
};
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
    uintptr_t aimLock=0, silentLock=0;
    std::atomic<bool> silentReady{false};
    std::atomic<uint64_t> redirectedRays{0};
    std::shared_ptr<const Frame> published=std::make_shared<const Frame>();
    Clock::time_point publishedAt{};
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
std::string managedString(void* text) {
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
    s.players.erase(reinterpret_cast<uintptr_t>(player));
}
// Read the inflated float field through IL2CPP, not guessed generic offsets (0x0 in dump).
// qXK is the primary Health value; runtime/device validation of obfuscated semantics is required.
bool readHealth(void* event,float& value) {
    struct Metadata {
        void* (*objectClass)(void*);
        void* (*findField)(void*,const char*);
        void (*getValue)(void*,void*,void*);
        const void* (*fieldType)(void*);
        int (*typeKind)(const void*);
    };
    static const Metadata m{
        reinterpret_cast<decltype(Metadata::objectClass)>(dlsym(RTLD_DEFAULT,"il2cpp_object_get_class")),
        reinterpret_cast<decltype(Metadata::findField)>(dlsym(RTLD_DEFAULT,"il2cpp_class_get_field_from_name")),
        reinterpret_cast<decltype(Metadata::getValue)>(dlsym(RTLD_DEFAULT,"il2cpp_field_get_value")),
        reinterpret_cast<decltype(Metadata::fieldType)>(dlsym(RTLD_DEFAULT,"il2cpp_field_get_type")),
        reinterpret_cast<decltype(Metadata::typeKind)>(dlsym(RTLD_DEFAULT,"il2cpp_type_get_type"))};
    if (!event || !m.objectClass || !m.findField || !m.getValue || !m.fieldType || !m.typeKind) return false;
    void* health=field<void*>(event,offsets::EntityHealth);
    if (!health) return false;
    // Cache metadata only, never a managed health object across frames/scenes.
    static void* cachedClass=nullptr; static void* cachedField=nullptr;
    void* klass=m.objectClass(health);
    if (!klass) return false;
    if (klass!=cachedClass) { cachedClass=klass; cachedField=m.findField(klass,"qXK"); }
    if (!cachedField || m.typeKind(m.fieldType(cachedField))!=0x0C) return false; // IL2CPP_TYPE_R4
    m.getValue(health,cachedField,&value);
    return std::isfinite(value) && value>=0 && value<1000000;
}
bool inactive(void* player) {
    return !player || field<uint8_t>(player,offsets::Respawning) ||
           field<uint8_t>(player,offsets::Kicked) || (field<int>(player,offsets::PlayerFlags)&2);
}
bool alive(void* player) {
    if (inactive(player)) return false;
    auto& s=state(); float health=0;
    return s.api.health && s.api.health(field<void*>(player,offsets::PlayerEvent),health) && health>0;
}
void publishFrame();
thread_local bool visibilityQuery=false;
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
    const bool wasLocal=player && player==s.localPlayer;
    if (wasLocal) { restoreCamera(); s.localPlayer=nullptr; s.aimLock=s.silentLock=0; s.players.clear(); }
    erase(player);
    // Invalidate the published entry immediately; retained render frames remain immutable.
    {
        std::lock_guard<std::mutex> guard(s.mutex);
        auto frame=wasLocal?std::make_shared<Frame>():std::make_shared<Frame>(*s.published);
        frame->boxes.erase(std::remove_if(frame->boxes.begin(),frame->boxes.end(),[&](const Box& box) {
            return box.id==reinterpret_cast<uintptr_t>(player);
        }),frame->boxes.end());
        frame->players=static_cast<unsigned>(frame->boxes.size());s.published=std::move(frame);
    }
    if (s.aimLock==reinterpret_cast<uintptr_t>(player)) s.aimLock=0;
    if (s.silentLock==reinterpret_cast<uintptr_t>(player)) s.silentLock=0;
    // Before the original callback can release references.
    if (state().api.disable) state().api.disable(player, method);
}
void applyLocal(void* player);
void onUpdate(void* player, Method method) {
    auto& s = state();
    if (s.api.update) s.api.update(player, method);
    if (!player || !s.ready.load(std::memory_order_acquire)) return;
    if (s.api.local(player,nullptr)) {
        if(s.localPlayer!=player) {
            // A new avatar invalidates all scene-bound targets and camera bindings.
            s.players.clear();
            s.aimLock=s.silentLock=0;
            restoreCamera();
            s.localPlayer=player;
        }
        erase(player); return;
    }
    if (!s.enabled.load()) return;
    // Capture world state only. Projection happens once, with one camera, in local LateUpdate.
    if (inactive(player)) { erase(player); return; }
    void* transform=s.api.transform(player,nullptr);
    if (!transform) { erase(player); return; }
    Vec3 feet=s.api.position(transform,nullptr);
    if (!modules::finite(feet)) { erase(player); return; }
    Vec3 head{feet.x,feet.y+1.6f,feet.z};
    if (void* anchor=field<void*>(player,offsets::WorldCameraRoot)) {
        Vec3 candidate=s.api.position(anchor,nullptr);
        float height=modules::length(candidate-feet);
        if (std::isfinite(height) && height>.5f && height<3.5f) head=candidate;
    }
    const uintptr_t id=reinterpret_cast<uintptr_t>(player);
    if (s.players.size()>=512 && !s.players.count(id)) return;
    auto& t=s.players[id];
    t.box.id=id; t.time=Clock::now(); t.feet=feet;t.head=head;
    t.right=s.api.right?s.api.right(transform,nullptr):Vec3{1,0,0};
    t.sleeping=(field<int>(player,offsets::PlayerFlags)&1)!=0;
    float health=0;
    void* event=field<void*>(player,offsets::PlayerEvent);
    t.healthKnown=s.api.health && s.api.health(event,health);
    t.health=health; t.dead=t.healthKnown && health<=0;
    t.box.healthKnown=t.healthKnown;t.box.health=health;
    void* vitals=field<void*>(player,offsets::Vitals);
    t.box.maxHealth=vitals?field<float>(vitals,offsets::MaxHealth):0;
    if (t.time-t.labelTime>=std::chrono::seconds(1)) {
        std::string label=event?managedString(field<void*>(event,offsets::EntityName)):std::string{};
        if (label.empty()) label=managedString(field<void*>(player,offsets::UserId));
        t.box.label=std::move(label);t.labelTime=t.time;
    }
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
    return std::isfinite(dt) && dt>0.f && dt<1.f ? std::clamp(dt,.001f,.1f) : 1.f/60.f;
}
bool visible(void* player,Vec3 origin,Vec3 point) {
    auto& s=state();
    if (!s.api.raycast || !s.api.hitTransform || !s.api.isChildOf) return false;
    Vec3 direction=point-origin; float distance=modules::length(direction);
    if (!modules::finite(direction) || !std::isfinite(distance) || distance<.01f) return false;
    RaycastHit hit{};
    modules::ScopedValue<bool> guard(&visibilityQuery,true,true);
    // DefaultRaycastLayers (-5); ignore triggers. Do not omit target colliders or fabricate hits.
    if (!s.api.raycast(origin,direction*(1.f/distance),&hit,distance,-5,1,nullptr)) return true;
    void* hitTransform=s.api.hitTransform(&hit,nullptr);
    void* targetTransform=s.api.transform(player,nullptr);
    return hitTransform && targetTransform &&
           (hitTransform==targetTransform || s.api.isChildOf(hitTransform,targetTransform,nullptr));
}
bool selectTarget(void* camera, int bone, float fov, float maxDistance, bool visibleOnly,
                  bool deathCheck, uintptr_t& lock, Vec3& result) {
    auto& s=state();
    void* transform=s.api.transform(camera,nullptr);
    if (!transform || !std::isfinite(fov) || fov<=0 || !std::isfinite(maxDistance)) { lock=0;return false; }
    const Vec3 origin=s.api.position(transform,nullptr);
    const int width=s.api.width(nullptr),height=s.api.height(nullptr);
    if (width<=0 || height<=0 || !modules::finite(origin)) { lock=0;return false; }
    const auto cutoff=Clock::now()-std::chrono::milliseconds(150);
    auto eligible=[&](const auto& entry,Vec3& point,float& metric) {
        const auto& t=entry.second;
        if (entry.first==reinterpret_cast<uintptr_t>(s.localPlayer) || t.time<cutoff || t.sleeping) return false;
        void* player=reinterpret_cast<void*>(entry.first);
        // The registry is game-thread owned and erased before original OnDisable.
        if (inactive(player) || (field<int>(player,offsets::PlayerFlags)&1) || (deathCheck && !alive(player))) return false;
        point=modules::bone(t.feet,t.head,t.right,bone);
        float distance=modules::length(point-origin);
        if (!std::isfinite(distance) || distance<.3f || distance>maxDistance) return false;
        Vec3 projected=s.api.project(camera,point,nullptr);
        if (!modules::finite(projected) || projected.z<=.1f) return false;
        float x=projected.x-width*.5f,y=projected.y-height*.5f;
        metric=x*x+y*y;
        return metric<fov*fov;
    };
    // Retain the same target strictly inside the FOV. No cross-target flicker, no grace through walls/death.
    auto locked=s.players.find(lock); Vec3 point; float metric=0;
    if (locked!=s.players.end() && eligible(*locked,point,metric) &&
        (!visibleOnly || visible(reinterpret_cast<void*>(lock),origin,point))) { result=point;return true; }
    lock=0;
    // Sort lightweight candidates; no per-candidate names/strings copied. Raycast only until first visible target.
    struct Candidate { uintptr_t id; Vec3 point; float metric; };
    std::vector<Candidate> candidates; candidates.reserve(s.players.size());
    for (const auto& entry:s.players) if (eligible(entry,point,metric)) candidates.push_back({entry.first,point,metric});
    std::sort(candidates.begin(),candidates.end(),[](const auto& a,const auto& b) {
        return a.metric==b.metric?a.id<b.id:a.metric<b.metric;
    });
    for (const auto& c:candidates) if (!visibleOnly || visible(reinterpret_cast<void*>(c.id),origin,c.point)) {
        lock=c.id; result=c.point;return true;
    }
    return false;
}
void applyLocal(void* player) {
    auto& s=state(); const auto opts=options(); const auto& v=opts.values;
    const float dt=delta(s.lastAim);
    if (inactive(player)) { s.aimLock=s.silentLock=0; restoreCamera(); return; }
    if (v.Menu.openmenu || v.FreeCam.enable) { s.aimLock=s.silentLock=0; return; }
    void* camera=s.api.camera(nullptr);
    if (!camera) return;
    void* fp=field<void*>(player,offsets::FpManager);
    float scope=fp?field<float>(fp,offsets::ScopeBlend):0.f;
    const bool scoped=std::isfinite(scope) && scope>.5f;
    if (v.Aimbot.enableAimbot && (!v.Aimbot.deathCheck || alive(player)) && (!v.Aimbot.scopeCheck || scoped) &&
        (v.Aimbot.aimBind==0 || v.Aimbot.aimBind==273 || opts.controls.aimHeld)) {
        Vec3 target;
        if (selectTarget(camera,v.Aimbot.selectedBoneIndex,v.Aimbot.fovcheck?v.Aimbot.fov:100000.f,
                         v.Aimbot.maxDistance,v.Aimbot.visibleOnly,v.Aimbot.deathCheck,s.aimLock,target)) {
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
    else s.aimLock=0;
}

// A redirect exists only while the original local FPHitscan.Update is on this thread.
// This is intentionally NOT an origin/position write and never changes camera aim.
struct SilentContext { bool active=false; Vec3 camera,forward,target; };
thread_local SilentContext silentContext;
bool onRaycast(const void* scene,const Ray* ray,float distance,RaycastHit* hit,int mask,int triggers,Method method) {
    auto& s=state();
    if (!s.api.raycastOriginal) return false;
    Ray replacement{};
    if (ray && silentContext.active && !visibilityQuery && modules::finite(ray->origin) && modules::finite(ray->direction) &&
        !std::isnan(distance) && distance>2.f &&
        modules::length(ray->origin-silentContext.camera)<2.f) {
        const float len=modules::length(ray->direction);
        Vec3 direction=silentContext.target-ray->origin;
        const float targetDistance=modules::length(direction);
        if (std::isfinite(len) && len>.01f && std::isfinite(targetDistance) && targetDistance>.01f &&
            targetDistance<=distance && modules::dot(ray->direction*(1.f/len),silentContext.forward)>.9f) {
            replacement={ray->origin,direction*(1.f/targetDistance)};
            ray=&replacement;s.redirectedRays.fetch_add(1,std::memory_order_relaxed);
        }
    }
    return s.api.raycastOriginal(scene,ray,distance,hit,mask,triggers,method);
}

void onLate(void* player,Method method) {
    auto& s=state();
    if (s.api.late) s.api.late(player,method);
    if (!player || player!=s.localPlayer || !s.ready.load()) return;
    if (!s.moduleReady.load()) { publishFrame(); return; }
    applyLocal(player);
    const auto opts=options(); const auto& v=opts.values; auto& c=s.camera;
    const float dt=delta(s.lastCamera);
    void* camera=s.api.camera(nullptr);
    if (!camera) {
        c={};
        { std::lock_guard<std::mutex> lock(s.mutex); s.publishedFreecam=false; }
        publishFrame();return;
    }
    if (c.camera!=camera) { c={}; c.camera=camera; }
    if (inactive(player)) { restoreCamera(); publishFrame(); return; }
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
    publishFrame();
}
void onWeaponUpdate(void* weapon,Method method) {
    auto& s=state(); const auto opts=options();
    if (!s.moduleReady.load() || !weapon || !s.localPlayer ||
        field<void*>(weapon,offsets::WeaponPlayer)!=s.localPlayer) {
        if (s.api.weaponUpdate) s.api.weaponUpdate(weapon,method);
        return;
    }
    const auto& v=opts.values;
    SilentContext context;
    if (s.silentReady.load() && v.Aimbot.silentAim && !v.Menu.openmenu && !v.FreeCam.enable &&
        !inactive(s.localPlayer) && (!v.Aimbot.silentDeathCheck || alive(s.localPlayer))) {
        void* fp=field<void*>(s.localPlayer,offsets::FpManager);
        const bool scoped=fp && field<float>(fp,offsets::ScopeBlend)>.5f;
        void* camera=s.api.camera(nullptr);
        if (camera && (!v.Aimbot.silentScopeCheck || scoped) &&
            selectTarget(camera,v.Aimbot.silentBoneIndex,v.Aimbot.silentFov,v.Aimbot.maxDistance,
                         v.Aimbot.silentVisibleOnly,v.Aimbot.silentDeathCheck,s.silentLock,context.target)) {
            void* transform=s.api.transform(camera,nullptr);
            context.camera=s.api.position(transform,nullptr);context.forward=s.api.forward(transform,nullptr);
            float length=modules::length(context.forward);
            context.active=modules::finite(context.camera) && std::isfinite(length) && length>.01f;
            if (context.active) context.forward=context.forward*(1.f/length);
        }
    } else s.silentLock=0;
    // RAII restores context even with nested callbacks; never leaks into other threads/frames.
    modules::ScopedValue<SilentContext> scopedContext(&silentContext,context,true);
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

namespace {
void publishFrame() {
    auto& s=state(); const auto now=Clock::now();
    auto frame=std::make_shared<Frame>();
    frame->boxes.reserve(s.players.size());
    float health=0;
    frame->localHealthKnown=s.localPlayer && s.api.health &&
        s.api.health(field<void*>(s.localPlayer,offsets::PlayerEvent),health);
    frame->localAlive=frame->localHealthKnown && health>0 && !inactive(s.localPlayer);
    frame->visibleReady=s.api.raycast && s.api.hitTransform && s.api.isChildOf;
    frame->silentHookReady=s.silentReady.load();
    void* camera=s.api.camera(nullptr);
    void* transform=camera?s.api.transform(camera,nullptr):nullptr;
    const int width=s.api.width(nullptr),height=s.api.height(nullptr);
    Vec3 origin=transform?s.api.position(transform,nullptr):Vec3{};
    for (auto it=s.players.begin();it!=s.players.end();) {
        auto& t=it->second;
        if (t.time<now-std::chrono::milliseconds(300)) { it=s.players.erase(it);continue; }
        ++it;
        if (!s.enabled.load() || !s.localPlayer || inactive(s.localPlayer) || t.dead || !camera || !transform ||
            !modules::finite(origin) || width<=0 || height<=0) continue;
        if (!t.healthKnown) ++frame->healthUnknown;
        Vec3 fp=s.api.project(camera,t.feet,nullptr),hp=s.api.project(camera,t.head,nullptr);
        auto& box=t.box;
        if (!modules::finite(fp) || !modules::finite(hp)) continue;
        box.depth=hp.z;box.projected=fp.z>.1f && hp.z>.1f;
        box.onScreen=box.projected && hp.x>=0 && hp.x<=width && hp.y>=0 && hp.y<=height;
        box.feetX=fp.x/width;box.feetY=1-fp.y/height;
        box.headX=hp.x/width;box.headY=1-hp.y/height;
        box.distance=modules::length(t.feet-origin);
        frame->boxes.push_back(box);++frame->players;
    }
    std::lock_guard<std::mutex> lock(s.mutex);
    frame->redirectedRays=s.redirectedRays.load(std::memory_order_relaxed);
    frame->freecam=s.publishedFreecam;frame->cameraPosition=s.publishedCamera;
    s.published=std::move(frame);s.publishedAt=now;
}
}
std::shared_ptr<const Frame> sharedSnapshot() {
    auto& s=state();
    static const auto empty=std::make_shared<const Frame>();
    std::lock_guard<std::mutex> lock(s.mutex);
    // Render owns immutable data; never follows managed pointers, prunes registry or copies labels.
    if (Clock::now()-s.publishedAt>std::chrono::milliseconds(300)) return empty;
    return s.published;
}
Frame snapshot() { return *sharedSnapshot(); }
void submit(const AllVars& options, modules::Controls controls) {
    auto& s=state();
    { std::lock_guard<std::mutex> lock(s.optionsMutex); s.options=options;s.controls=controls; }
    setEnabled(options.ESP.enable || options.Chams.enable || options.Aimbot.enableAimbot || options.Aimbot.silentAim);
}
void setEnabled(bool enabled) {
    auto& s = state();
    s.enabled.store(enabled);
    if (!enabled) {
        std::lock_guard<std::mutex> lock(s.mutex);
        // Preserve diagnostics/freecam even when only visual/aim modules are off.
        if (!s.published->boxes.empty()) {
            auto frame=std::make_shared<Frame>();
            frame->freecam=s.publishedFreecam;frame->cameraPosition=s.publishedCamera;
            frame->redirectedRays=s.redirectedRays.load(std::memory_order_relaxed);
            s.published=std::move(frame);
        }
    }
}
const char* status() { return state().message.load(); }
void installGameHooks() {
    auto& s = state();
    s.api.health=readHealth;
    uintptr_t update = 0;
    for (int i = 0; i < 120 && !update; ++i) {
        update = resolve(offsets::PlayerUpdate);
        if (!update) sleep(1);
    }
    const uintptr_t disable = resolve(offsets::PlayerOnDisable);
    const uintptr_t late = resolve(offsets::PlayerLateUpdate);
    s.api.raycast=reinterpret_cast<decltype(s.api.raycast)>(resolve(offsets::PhysicsRaycast));
    s.api.hitTransform=reinterpret_cast<decltype(s.api.hitTransform)>(resolve(offsets::HitTransform));
    s.api.isChildOf=reinterpret_cast<decltype(s.api.isChildOf)>(resolve(offsets::IsChildOf));
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
    if (!update || !disable || !late || !s.api.project || !s.api.position || !s.api.transform ||
        !s.api.camera || !s.api.local || !s.api.width || !s.api.height) {
        s.message.store("ERROR: IL2CPP RVAs unavailable; check exact game build");
    } else if (DobbyHook(reinterpret_cast<void*>(disable), reinterpret_cast<void*>(onDisable),
                         reinterpret_cast<void**>(&s.api.disable)) != 0) {
        s.message.store("ERROR: OnDisable hook failed");
    } else if (DobbyHook(reinterpret_cast<void*>(update), reinterpret_cast<void*>(onUpdate),
                         reinterpret_cast<void**>(&s.api.update)) != 0) {
        s.message.store("ERROR: Update hook failed");
    } else if (DobbyHook(reinterpret_cast<void*>(late),reinterpret_cast<void*>(onLate),
                         reinterpret_cast<void**>(&s.api.late))!=0) {
        s.message.store("ERROR: LateUpdate projection hook failed");
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
        hook(offsets::WeaponUpdate,onWeaponUpdate,s.api.weaponUpdate);
        hook(offsets::FpLateUpdate,onFpLate,s.api.fpLate);
        const uintptr_t raycast=resolve(offsets::RaycastInjected);
        s.silentReady.store(raycast && DobbyHook(reinterpret_cast<void*>(raycast),
            reinterpret_cast<void*>(onRaycast),reinterpret_cast<void**>(&s.api.raycastOriginal))==0);
        s.moduleReady.store(ok);
        s.message.store(!ok?"ESP ready / ERROR: gameplay module hooks failed":
            !s.silentReady.load()?"ESP + Aim ready / ERROR: silent ray hook failed":
            "Internal hooks ready / Silent Ray EXPERIMENT (device validation required)");
    } else if (s.ready.load()) s.message.store("ESP ready / ERROR: module API unavailable");
    __android_log_print(ANDROID_LOG_INFO, "EncoreDLC", "%s", status());
}
}
