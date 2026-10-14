#include "../jni/src/Runtime.cpp"
#include <array>
#include <cassert>
extern "C" int DobbyHook(void*, void*, void**) { return -1; }
namespace {
int updates=0, disables=0, cameraObject=0;
bool local=false;
float cameraFov=60,cameraAspect=1.7f;
runtime::Vec3 cameraPosition{};
runtime::Quaternion cameraRotation{};
void update(void*,const void*) { ++updates; }
void disable(void*,const void*) { ++disables; }
}
int main() {
    auto& s=runtime::state();
    s.api.update=update; s.api.disable=disable;
    s.api.local=[](void*,const void*) { return local; };
    s.api.camera=[](const void*) -> void* { return &cameraObject; };
    s.api.transform=[](void* p,const void*) { return p; };
    s.api.position=[](void* p,const void*) -> runtime::Vec3 {
        return p==&cameraObject?cameraPosition:runtime::Vec3{0,0,10};
    };
    s.api.project=[](void*,runtime::Vec3 p,const void*) -> runtime::Vec3 { return {50+p.x*10,50+p.y*10,p.z}; };
    s.api.width=s.api.height=[](const void*) { return 100; };
    s.ready.store(true);
    alignas(8) std::array<unsigned char,0x300> player{};
    runtime::onUpdate(player.data(),nullptr);
    assert(updates==1 && runtime::snapshot().players==0);
    runtime::setEnabled(true);
    runtime::onUpdate(player.data(),nullptr);
    auto frame=runtime::snapshot();
    assert(frame.players==1 && frame.boxes[0].projected && frame.boxes[0].distance==10);
    assert(std::fabs(frame.boxes[0].headY-0.34f)<0.001f);
    player[offsets::Respawning]=1;
    runtime::onUpdate(player.data(),nullptr);
    assert(runtime::snapshot().players==0);
    player[offsets::Respawning]=0;
    local=true; runtime::onUpdate(player.data(),nullptr);
    assert(runtime::snapshot().players==0);
    local=false; runtime::onUpdate(player.data(),nullptr);
    runtime::onDisable(player.data(),nullptr);
    assert(disables==1 && runtime::snapshot().players==0);
    runtime::onUpdate(player.data(),nullptr);
    s.players.begin()->second.time-=std::chrono::seconds(1);
    assert(runtime::snapshot().players==0);
    runtime::onUpdate(player.data(),nullptr);
    runtime::setEnabled(false);
    assert(runtime::snapshot().players==0);
    s.api.rotation=[](void*,const void*) { return cameraRotation; };
    s.api.setRotation=[](void*,runtime::Quaternion q,const void*) { cameraRotation=q; };
    s.api.setPosition=[](void*,runtime::Vec3 p,const void*) { cameraPosition=p; };
    s.api.forward=[](void*,const void*) -> runtime::Vec3 { return {0,0,1}; };
    s.api.right=[](void*,const void*) -> runtime::Vec3 { return {1,0,0}; };
    s.api.fov=[](void*,const void*) { return cameraFov; };
    s.api.setFov=[](void*,float v,const void*) { cameraFov=v; };
    s.api.aspect=[](void*,const void*) { return cameraAspect; };
    s.api.setAspect=[](void*,float v,const void*) { cameraAspect=v; };
    s.moduleReady.store(true);local=true;
    AllVars options;options.FreeCam.enable=true;options.Player.camerafov=true;
    options.Player.customFov=100;options.Player.aspect_ratio=true;options.Player.aspect_value=2;
    runtime::submit(options,{0,-1,0,0,0,false});
    runtime::onUpdate(player.data(),nullptr);runtime::onLate(player.data(),nullptr);
    assert(cameraPosition.z>0 && cameraFov==100 && cameraAspect==2 && runtime::snapshot().freecam);
    runtime::submit(AllVars{},{});runtime::onLate(player.data(),nullptr);
    assert(cameraPosition.z==0 && cameraFov==60 && cameraAspect==1.7f && !runtime::snapshot().freecam);
    // No-recoil/spread/arms overrides are scoped to the original callback.
    alignas(8) std::array<unsigned char,0x300> weapon{},fp{},config{},recoil{},spread{};
    *runtime::address<void*>(weapon.data(),offsets::WeaponPlayer)=player.data();
    *runtime::address<void*>(weapon.data(),offsets::WeaponConfig)=config.data();
    *runtime::address<void*>(config.data(),offsets::RecoilConfig)=recoil.data();
    *runtime::address<void*>(config.data(),offsets::DispersionConfig)=spread.data();
    *runtime::address<runtime::Vec2>(recoil.data(),offsets::MinRecoil)={2,3};
    *runtime::address<float>(spread.data(),offsets::MinDispersion)=7;
    *runtime::address<void*>(player.data(),offsets::FpManager)=fp.data();
    *runtime::address<void*>(fp.data(),offsets::CurrentWeapon)=weapon.data();
    *runtime::address<int>(weapon.data(),offsets::NormalFov)=60;
    *runtime::address<int>(weapon.data(),offsets::AimFov)=40;
    options=AllVars{};options.Player.norecoil=true;options.Player.nospread=true;
    options.Player.armfov=true;options.Player.armfovValue=90;
    runtime::submit(options,{});
    s.api.weaponUpdate=[](void* weapon,const void*) {
        void* config=runtime::field<void*>(weapon,offsets::WeaponConfig);
        void* recoil=runtime::field<void*>(config,offsets::RecoilConfig);
        void* spread=runtime::field<void*>(config,offsets::DispersionConfig);
        assert(runtime::field<runtime::Vec2>(recoil,offsets::MinRecoil).x==0);
        assert(runtime::field<float>(spread,offsets::MinDispersion)==0);
    };
    runtime::onWeaponUpdate(weapon.data(),nullptr);
    assert(runtime::field<runtime::Vec2>(recoil.data(),offsets::MinRecoil).x==2);
    assert(runtime::field<float>(spread.data(),offsets::MinDispersion)==7);
    s.api.fpLate=[](void* fp,const void*) {
        void* w=runtime::field<void*>(fp,offsets::CurrentWeapon);
        assert(runtime::field<int>(w,offsets::NormalFov)==90 && runtime::field<int>(w,offsets::AimFov)==90);
    };
    runtime::onFpLate(fp.data(),nullptr);
    assert(runtime::field<int>(weapon.data(),offsets::NormalFov)==60 && runtime::field<int>(weapon.data(),offsets::AimFov)==40);
    // A pending origin override must restore even when the menu opens/no target remains.
    options=AllVars{};options.Aimbot.silentAim=true;options.Aimbot.silentFov=400;
    runtime::submit(options,{});
    runtime::TimedBox target;target.time=runtime::Clock::now();target.feet={0,0,10};target.head={0,1.6f,10};target.right={1,0,0};
    s.players[123]=target;
    *runtime::address<runtime::Vec3>(player.data(),offsets::LastTickPosition)={1,2,3};
    runtime::onUpdate(player.data(),nullptr);
    assert(s.silentSaved && runtime::field<runtime::Vec3>(player.data(),offsets::LastTickPosition).z==10);
    options.Menu.openmenu=true;runtime::submit(options,{});s.players.clear();
    runtime::onUpdate(player.data(),nullptr);
    assert(!s.silentSaved && runtime::field<runtime::Vec3>(player.data(),offsets::LastTickPosition).z==3);
    // Disable invalidates local bindings and never carries an old avatar's saved state forward.
    runtime::onDisable(player.data(),nullptr);assert(s.localPlayer==nullptr);
    // The returned frame owns data; removing player state cannot invalidate it.
    assert(frame.boxes[0].distance==10);
}
