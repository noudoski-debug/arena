#include "../jni/src/Runtime.cpp"
#include <array>
#include <cassert>
extern "C" int DobbyHook(void*, void*, void**) { return -1; }
namespace {
int updates=0, disables=0, cameraObject=0;
bool local=false;
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
        return p==&cameraObject?runtime::Vec3{0,0,0}:runtime::Vec3{0,0,10};
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
    // The returned frame owns data; removing player state cannot invalidate it.
    assert(frame.boxes[0].distance==10);
}
