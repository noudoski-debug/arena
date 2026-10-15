#pragma once
#include "dobby.h"
#include "Diagnostics.h"
#include <mutex>
namespace hooks {
inline std::mutex& mutex() {static std::mutex lock;return lock;}
inline int install(void* address,void* replacement,void** original,const char* name) {
    diagnostics::log("HOOK %s waiting address=%p",name,address);
    // EGL, input and game bootstrap workers must not mutate Dobby state concurrently.
    std::lock_guard<std::mutex> lock(mutex());
    diagnostics::log("HOOK %s begin",name);
    const int result=DobbyHook(address,replacement,original);
    diagnostics::log("HOOK %s end rc=%d trampoline=%p",name,result,original?*original:nullptr);
    return result;
}
}
