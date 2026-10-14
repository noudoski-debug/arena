#include "../jni/src/Runtime.cpp"
#include <cassert>
#include <thread>
#include <vector>

namespace {
std::atomic<int> realHooks{0}, launches{0};
int hookCalls=0, failure=0, forwarded=0;
uintptr_t missing=0;
void original(void*,const void*) { ++forwarded; }
uintptr_t resolveMock(uintptr_t rva) { return rva==missing?0:rva; }
int hookMock(void*,void*,void** saved) {
    if (++hookCalls==failure) return -1;
    *saved=reinterpret_cast<void*>(original);return 0;
}
int holdWorker(pthread_t*,const pthread_attr_t* attr,void* (*)(void*),void*) {
    int detached=0;assert(pthread_attr_getdetachstate(attr,&detached)==0);
    assert(detached==PTHREAD_CREATE_DETACHED);
    ++launches;return 0; // Tests reserve the worker without executing it.
}
int failWorker(pthread_t*,const pthread_attr_t*,void* (*)(void*),void*) { ++launches;return EAGAIN; }
void reset() {
    auto& s=runtime::state();
    s.api={};s.options=AllVars{};s.controls={};s.players.clear();s.localPlayer=nullptr;
    s.silentSaved=false;s.enabled=false;s.ready=false;s.moduleReady=false;
    s.hookStage=runtime::HookStage::MenuOnly;
    hookCalls=0;failure=0;missing=0;forwarded=0;launches=0;
}
}
extern "C" int DobbyHook(void*,void*,void**) { ++realHooks;return -1; }
int main() {
    auto& s=runtime::state();
    assert(runtime::hookStage()==runtime::HookStage::MenuOnly);
    AllVars v;v.ESP.enable=true;v.Aimbot.enableAimbot=true;v.FreeCam.enable=true;
    v.Player.norecoil=true;v.Aimbot.silentAim=true;
    runtime::submit(v,{0,0,0,0,0,true});
    assert(runtime::hookStage()==runtime::HookStage::MenuOnly && realHooks==0);
    assert(!s.enabled && !s.options.Aimbot.enableAimbot && !s.options.ESP.enable);
    assert(!s.options.FreeCam.enable && !s.options.Player.norecoil && !s.controls.aimHeld);

    // Every required RVA is validated BEFORE any hook is installed.
    const uintptr_t rvas[]={
        offsets::PhysicsLinecast,offsets::HitTransform,offsets::IsChildOf,
        offsets::WorldToScreenPoint,offsets::GetPosition,offsets::GetTransform,
        offsets::GetMainCamera,offsets::IsLocalPlayer,offsets::GetScreenWidth,offsets::GetScreenHeight,
        offsets::SetPosition,offsets::GetRotation,offsets::SetRotation,offsets::GetForward,offsets::GetRight,
        offsets::GetFov,offsets::SetFov,offsets::GetAspect,offsets::SetAspect,
        offsets::PlayerOnDisable,offsets::PlayerUpdate,offsets::PlayerLateUpdate,
        offsets::WeaponUpdate,offsets::FpLateUpdate,
    };
    for (auto rva:rvas) {
        reset();missing=rva;
        assert(!runtime::installGameHooks(resolveMock,hookMock));
        assert(hookCalls==0 && !s.ready && !s.moduleReady);
    }
    // Stop immediately on patch failure; callbacks remain pass-through until ALL succeed.
    for (int fail=1;fail<=5;++fail) {
        reset();failure=fail;
        assert(!runtime::installGameHooks(resolveMock,hookMock));
        assert(hookCalls==fail && !s.ready && !s.moduleReady);
        void* unused=reinterpret_cast<void*>(1);
        runtime::onUpdate(unused,nullptr);runtime::onDisable(unused,nullptr);
        runtime::onLate(unused,nullptr);runtime::onWeaponUpdate(unused,nullptr);runtime::onFpLate(unused,nullptr);
        assert(forwarded==fail-1);
    }
    reset();
    assert(runtime::installGameHooks(resolveMock,hookMock));
    assert(hookCalls==5 && s.ready && s.moduleReady);
    s.hookStage=runtime::HookStage::Ready;
    runtime::submit(v,{});assert(s.enabled && s.options.Aimbot.enableAimbot);
    assert(!runtime::requestGameHooksWith(holdWorker) && launches==0);

    reset();
    std::atomic<int> accepted{0};std::vector<std::thread> callers;
    for (int i=0;i<16;++i) callers.emplace_back([&] {
        if (runtime::requestGameHooksWith(holdWorker)) ++accepted;
    });
    for (auto& thread:callers) thread.join();
    assert(accepted==1 && launches==1 && runtime::hookStage()==runtime::HookStage::Connecting);
    runtime::submit(v,{});assert(!s.enabled && !s.options.Aimbot.enableAimbot);

    reset();
    assert(!runtime::requestGameHooksWith(failWorker));
    assert(runtime::hookStage()==runtime::HookStage::Failed && launches==1);
    assert(!runtime::requestGameHooksWith(holdWorker) && launches==1);
    runtime::submit(v,{});assert(!s.enabled);

    // Actual asynchronous path: no libil2cpp on the host, no blocking 120-second wait.
    reset();assert(runtime::requestGameHooks());
    const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(2);
    while (runtime::hookStage()==runtime::HookStage::Connecting && std::chrono::steady_clock::now()<deadline)
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    assert(runtime::hookStage()==runtime::HookStage::Failed && realHooks==0);
    assert(!runtime::requestGameHooks());
}
