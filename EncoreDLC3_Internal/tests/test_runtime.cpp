#include "../jni/src/Runtime.cpp"
#include <array>
#include <cassert>
#include <limits>
#include <thread>
extern "C" int DobbyHook(void*, void*, void**) { return -1; }
namespace {
using Object=std::array<unsigned char,0x400>;
alignas(8) Object localPlayer{},enemy{},other{},look{},weapon{},fp{},config{},recoil{},spread{};
int updates=0,disables=0,cameraObject=0,wall=0,limb=0,projectionCalls=0;
float localHealth=100,enemyHealth=100,otherHealth=100;
bool knownHealth=true,obstructed=false;
void* hitObject=nullptr;
float cameraFov=60,cameraAspect=1.7f;
runtime::Vec3 cameraPosition{},enemyPosition{2,0,10},otherPosition{1,0,10};
runtime::Quaternion cameraRotation{};
runtime::Ray lastRay;
bool near(float a,float b) { return std::fabs(a-b)<.0001f; }
void capture() { runtime::onUpdate(enemy.data(),nullptr);runtime::onLate(localPlayer.data(),nullptr); }
void setup() {
    auto& s=runtime::state();
    s.api.update=[](void*,const void*){++updates;};
    s.api.disable=[](void*,const void*){++disables;};
    s.api.local=[](void* p,const void*){return p==localPlayer.data();};
    s.api.camera=[](const void*) -> void* {return &cameraObject;};
    s.api.transform=[](void* p,const void*){return p;};
    s.api.position=[](void* p,const void*) -> runtime::Vec3 {
        if(p==&cameraObject)return cameraPosition;
        if(p==enemy.data())return enemyPosition;
        if(p==other.data())return otherPosition;
        return {};
    };
    s.api.project=[](void*,runtime::Vec3 p,const void*) -> runtime::Vec3 {
        ++projectionCalls;return {50+p.x*10,50+p.y*10,p.z};
    };
    s.api.width=s.api.height=[](const void*){return 100;};
    s.api.rotation=[](void*,const void*){return cameraRotation;};
    s.api.setRotation=[](void*,runtime::Quaternion q,const void*){cameraRotation=q;};
    s.api.setPosition=[](void*,runtime::Vec3 p,const void*){cameraPosition=p;};
    s.api.forward=[](void*,const void*) -> runtime::Vec3 {return {0,0,1};};
    s.api.right=[](void*,const void*) -> runtime::Vec3 {return {1,0,0};};
    s.api.fov=[](void*,const void*){return cameraFov;};
    s.api.setFov=[](void*,float v,const void*){cameraFov=v;};
    s.api.aspect=[](void*,const void*){return cameraAspect;};
    s.api.setAspect=[](void*,float v,const void*){cameraAspect=v;};
    s.api.health=[](void* p,float& value){
        if(!knownHealth || !p)return false;
        value=p==localPlayer.data()?localHealth:p==enemy.data()?enemyHealth:otherHealth;
        return std::isfinite(value);
    };
    s.api.raycast=[](runtime::Vec3,runtime::Vec3 direction,runtime::RaycastHit*,float distance,int mask,int triggers,const void*) {
        assert(near(modules::length(direction),1) && distance>0 && mask==-5 && triggers==1);
        assert(runtime::visibilityQuery);return obstructed;
    };
    s.api.hitTransform=[](runtime::RaycastHit*,const void*){return hitObject;};
    s.api.isChildOf=[](void* child,void* parent,const void*){return child==&limb && parent==enemy.data();};
    s.api.raycastOriginal=[](const void*,const runtime::Ray* ray,float,runtime::RaycastHit*,int,int,const void*) {
        lastRay=*ray;return true;
    };
    // Event objects use real backing storage, never invented registry addresses.
    for(auto* p:{&localPlayer,&enemy,&other})
        *runtime::address<void*>(p->data(),offsets::PlayerEvent)=p->data();
    *runtime::address<void*>(localPlayer.data(),offsets::MouseLook)=look.data();
    *runtime::address<void*>(localPlayer.data(),offsets::FpManager)=fp.data();
    *runtime::address<void*>(weapon.data(),offsets::WeaponPlayer)=localPlayer.data();
    *runtime::address<void*>(weapon.data(),offsets::WeaponConfig)=config.data();
    *runtime::address<void*>(fp.data(),offsets::CurrentWeapon)=weapon.data();
    s.ready.store(true);
    runtime::onUpdate(localPlayer.data(),nullptr);
}
}
int main() {
    setup();auto& s=runtime::state();
    AllVars options;options.ESP.enable=true;runtime::submit(options,{});
    runtime::onUpdate(enemy.data(),nullptr);
    assert(projectionCalls==0); // no per-entity camera/projection work in Update
    runtime::onLate(localPlayer.data(),nullptr);
    auto frame=runtime::sharedSnapshot();
    assert(frame->players==1 && frame->boxes[0].projected && near(frame->boxes[0].headY,.34f));
    const int calls=projectionCalls;
    for(int i=0;i<1000;++i)assert(runtime::sharedSnapshot()==frame);
    assert(projectionCalls==calls); // render reuse is O(1), no strings/vector copies
    capture();assert(runtime::sharedSnapshot()!=frame && frame->players==1);
    enemy[offsets::Respawning]=1;capture();assert(runtime::snapshot().players==0);
    enemy[offsets::Respawning]=0;
    *runtime::address<int>(enemy.data(),offsets::PlayerFlags)=2;
    capture();assert(runtime::snapshot().players==0);
    *runtime::address<int>(enemy.data(),offsets::PlayerFlags)=0;
    enemyHealth=0;capture();assert(runtime::snapshot().players==0);
    enemyHealth=100;capture();
    runtime::onDisable(enemy.data(),nullptr);
    assert(disables==1 && runtime::snapshot().players==0 && frame->players==1);
    capture();s.players.begin()->second.time-=std::chrono::seconds(1);
    runtime::publishFrame();assert(runtime::snapshot().players==0);
    capture();s.publishedAt-=std::chrono::seconds(1);assert(runtime::snapshot().players==0);
    capture();runtime::setEnabled(false);assert(runtime::snapshot().players==0);
    runtime::submit(options,{});capture();

    // Physics LOS: clear path, wall, target collider, target child, API unavailable.
    auto origin=runtime::Vec3{},point=runtime::Vec3{2,1.6f,10};
    assert(runtime::visible(enemy.data(),origin,point));
    obstructed=true;hitObject=&wall;assert(!runtime::visible(enemy.data(),origin,point));
    hitObject=enemy.data();assert(runtime::visible(enemy.data(),origin,point));
    hitObject=&limb;assert(runtime::visible(enemy.data(),origin,point));
    hitObject=other.data();assert(!runtime::visible(enemy.data(),origin,point));
    auto raycast=s.api.raycast;s.api.raycast=nullptr;
    assert(!runtime::visible(enemy.data(),origin,point));s.api.raycast=raycast;
    obstructed=false;

    // Independent sticky locks; revalidate death/sleep/FOV/LOS immediately, including before next remote Update.
    uintptr_t lock=0;runtime::Vec3 selected;
    auto select=[&](bool vis=true,bool death=true,float fov=100.f) {
        return runtime::selectTarget(&cameraObject,0,fov,300,vis,death,lock,selected);
    };
    assert(select() && lock==reinterpret_cast<uintptr_t>(enemy.data()));
    runtime::onUpdate(other.data(),nullptr);assert(select() && lock==reinterpret_cast<uintptr_t>(enemy.data()));
    enemyHealth=0;assert(select() && lock==reinterpret_cast<uintptr_t>(other.data()));
    otherHealth=0;assert(!select() && lock==0);
    assert(select(false,false)); // explicit death-filter opt-out
    enemyHealth=otherHealth=100;
    knownHealth=false;assert(!select());knownHealth=true;
    *runtime::address<int>(enemy.data(),offsets::PlayerFlags)=1;
    *runtime::address<int>(other.data(),offsets::PlayerFlags)=1;
    assert(!select(false,false));
    *runtime::address<int>(enemy.data(),offsets::PlayerFlags)=0;
    *runtime::address<int>(other.data(),offsets::PlayerFlags)=0;
    obstructed=true;hitObject=&wall;assert(!select());obstructed=false;
    assert(!select(false,true,1));
    s.players[reinterpret_cast<uintptr_t>(enemy.data())].time-=std::chrono::seconds(1);
    s.players[reinterpret_cast<uintptr_t>(other.data())].time-=std::chrono::seconds(1);
    assert(!select());capture();runtime::onDisable(other.data(),nullptr);

    // Local Aim writes coherent yaw/pitch, and refuses unknown/dead local state.
    s.moduleReady.store(true);
    options.Aimbot.enableAimbot=true;options.Aimbot.scopeCheck=false;options.Aimbot.smooth=45;
    runtime::submit(options,{});runtime::applyLocal(localPlayer.data());
    auto angles=runtime::field<runtime::Vec2>(look.data(),offsets::MouseAngles);
    assert(angles.y>0 && near(angles.x,runtime::field<float>(localPlayer.data(),offsets::LookAngle)));
    localHealth=0;runtime::applyLocal(localPlayer.data());
    assert(runtime::field<runtime::Vec2>(look.data(),offsets::MouseAngles).y==angles.y && s.aimLock==0);
    localHealth=100;

    // Camera overrides still restore after disable.
    options=AllVars{};options.FreeCam.enable=true;options.Player.camerafov=true;
    options.Player.customFov=100;options.Player.aspect_ratio=true;options.Player.aspect_value=2;
    runtime::submit(options,{0,-1,0,0,0,false});runtime::onLate(localPlayer.data(),nullptr);
    assert(cameraPosition.z>0 && cameraFov==100 && cameraAspect==2 && runtime::snapshot().freecam);
    runtime::submit(AllVars{},{});runtime::onLate(localPlayer.data(),nullptr);
    assert(cameraPosition.z==0 && cameraFov==60 && cameraAspect==1.7f && !runtime::snapshot().freecam);

    // Weapon config overrides remain callback-scoped.
    *runtime::address<void*>(config.data(),offsets::RecoilConfig)=recoil.data();
    *runtime::address<void*>(config.data(),offsets::DispersionConfig)=spread.data();
    *runtime::address<runtime::Vec2>(recoil.data(),offsets::MinRecoil)={2,3};
    *runtime::address<float>(spread.data(),offsets::MinDispersion)=7;
    *runtime::address<int>(weapon.data(),offsets::NormalFov)=60;
    *runtime::address<int>(weapon.data(),offsets::AimFov)=40;
    options=AllVars{};options.Player.norecoil=options.Player.nospread=options.Player.armfov=true;
    options.Player.armfovValue=90;runtime::submit(options,{});
    s.api.weaponUpdate=[](void*,const void*) {
        assert(runtime::field<runtime::Vec2>(recoil.data(),offsets::MinRecoil).x==0);
        assert(runtime::field<float>(spread.data(),offsets::MinDispersion)==0);
    };
    runtime::onWeaponUpdate(weapon.data(),nullptr);
    assert(runtime::field<runtime::Vec2>(recoil.data(),offsets::MinRecoil).x==2);
    assert(runtime::field<float>(spread.data(),offsets::MinDispersion)==7);
    s.api.fpLate=[](void*,const void*) {
        assert(runtime::field<int>(weapon.data(),offsets::NormalFov)==90);
        assert(runtime::field<int>(weapon.data(),offsets::AimFov)==90);
    };
    runtime::onFpLate(fp.data(),nullptr);
    assert(runtime::field<int>(weapon.data(),offsets::NormalFov)==60);
    assert(runtime::field<int>(weapon.data(),offsets::AimFov)==40);

    // Silent never touches lastTickPosition or aim. It redirects only a qualifying ray in local weapon callback.
    options=AllVars{};options.Aimbot.silentAim=true;options.Aimbot.silentVisibleOnly=true;
    runtime::submit(options,{});capture();s.silentReady.store(true);
    *runtime::address<runtime::Vec3>(localPlayer.data(),offsets::LastTickPosition)={1,2,3};
    s.api.weaponUpdate=[](void*,const void*) {
        runtime::Ray ray{{0,0,0},{0,0,1}};runtime::RaycastHit hit;
        runtime::onRaycast(nullptr,&ray,100,&hit,-5,1,nullptr);
    };
    runtime::onWeaponUpdate(weapon.data(),nullptr);
    assert(s.redirectedRays.load()==1 && lastRay.direction.x>0 && lastRay.direction.y>0);
    assert(!runtime::silentContext.active);
    assert(runtime::field<runtime::Vec3>(localPlayer.data(),offsets::LastTickPosition).z==3);
    assert(runtime::field<runtime::Vec2>(look.data(),offsets::MouseAngles).y==angles.y);
    runtime::Ray ray{{0,0,0},{0,0,1}};runtime::RaycastHit hit;
    runtime::onRaycast(nullptr,&ray,100,&hit,-5,1,nullptr);
    assert(s.redirectedRays.load()==1 && lastRay.direction.x==0);
    for(int reason=0;reason<5;++reason) {
        options.Menu.openmenu=reason==0;enemyHealth=reason==1?0:100;localHealth=reason==2?0:100;
        knownHealth=reason!=3;obstructed=reason==4;hitObject=&wall;
        runtime::submit(options,{});runtime::onWeaponUpdate(weapon.data(),nullptr);
        assert(s.redirectedRays.load()==1 && !runtime::silentContext.active);
    }
    options.Menu.openmenu=false;enemyHealth=localHealth=100;knownHealth=true;obstructed=false;
    options.Aimbot.silentAim=false;runtime::submit(options,{});
    runtime::onWeaponUpdate(weapon.data(),nullptr);assert(s.redirectedRays.load()==1);
    options.Aimbot.silentAim=true;options.Aimbot.silentScopeCheck=true;runtime::submit(options,{});
    runtime::onWeaponUpdate(weapon.data(),nullptr);assert(s.redirectedRays.load()==1);
    options.Aimbot.silentScopeCheck=false;runtime::submit(options,{});
    s.silentReady.store(false);runtime::onWeaponUpdate(weapon.data(),nullptr);assert(s.redirectedRays.load()==1);
    s.silentReady.store(true);
    runtime::submit(options,{});
    s.api.weaponUpdate=[](void*,const void*) {assert(runtime::silentContext.active);throw 1;};
    try {runtime::onWeaponUpdate(weapon.data(),nullptr);assert(false);}catch(int){}
    assert(!runtime::silentContext.active);
    {
        modules::ScopedValue<runtime::SilentContext> guard(&runtime::silentContext,{true,{0,0,0},{0,0,1},{2,1,10}},true);
        std::thread otherThread([&] {runtime::onRaycast(nullptr,&ray,100,&hit,-5,1,nullptr);});
        otherThread.join();assert(s.redirectedRays.load()==1);
        runtime::visibilityQuery=true;runtime::onRaycast(nullptr,&ray,100,&hit,-5,1,nullptr);
        runtime::visibilityQuery=false;assert(s.redirectedRays.load()==1);
        runtime::Ray backward{{0,0,0},{0,0,-1}};
        runtime::onRaycast(nullptr,&backward,100,&hit,-5,1,nullptr);assert(s.redirectedRays.load()==1);
    }
    runtime::onDisable(localPlayer.data(),nullptr);
    assert(s.localPlayer==nullptr && s.aimLock==0 && s.silentLock==0 && runtime::snapshot().players==0);
    assert(frame->players==1); // retained snapshots never alias live registry
}
