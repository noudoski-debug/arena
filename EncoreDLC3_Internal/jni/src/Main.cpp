#include "imgui.h"
#include "imgui_impl_opengl3.h"
#include "Roboto-Regular.h"
#include "Hooks.h"
#include "Input.h"
#include "Menu.h"
#include "Runtime.h"
#include "Target.h"
#include <EGL/egl.h>
#include <android/log.h>
#include <dlfcn.h>
#include <pthread.h>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <mutex>

namespace {
using Swap = EGLBoolean (*)(EGLDisplay, EGLSurface);
Swap originalSwap = nullptr;
std::atomic<bool> inputReady{false}, sawSwap{false}, sawFrame{false};
std::atomic<unsigned> reportedSkips{0};
struct Renderer {
    std::mutex mutex;
    ImGuiContext* gui = nullptr;
    EGLContext context = EGL_NO_CONTEXT;
    EGLSurface surface = EGL_NO_SURFACE;
    std::chrono::steady_clock::time_point last;
    bool failed = false;
};
Renderer& renderer() { static Renderer r; return r; }
EGLBoolean swap(EGLDisplay display, EGLSurface surface) {
    if (!sawSwap.exchange(true)) diagnostics::log("EGL first eglSwapBuffers callback");
    if (!originalSwap) { diagnostics::log("EGL trampoline unavailable");return EGL_FALSE; }
    auto& r = renderer();
    std::unique_lock<std::mutex> lock(r.mutex, std::try_to_lock);
    EGLint width = 0, height = 0, version = 0;
    const EGLContext current = eglGetCurrentContext();
    auto skip=[&](unsigned bit,const char* reason) {
        if (!(reportedSkips.fetch_or(bit)&bit))
            diagnostics::log("EGL skip: %s; context=%p version=%d size=%dx%d",reason,current,version,width,height);
        return originalSwap(display,surface);
    };
    if (!lock.owns_lock()) return originalSwap(display,surface);
    if (r.failed) return skip(1,"renderer previously failed");
    if (current==EGL_NO_CONTEXT) return skip(2,"no current EGL context");
    if (!eglQueryContext(display,current,EGL_CONTEXT_CLIENT_VERSION,&version)) return skip(4,"query context failed");
    if (version<3) return skip(8,"OpenGL ES 3 required");
    if (!eglQuerySurface(display,surface,EGL_WIDTH,&width) ||
        !eglQuerySurface(display,surface,EGL_HEIGHT,&height)) return skip(16,"query surface failed");
    if (width<100 || height<100) return skip(32,"surface too small");
    if (r.gui && r.context!=current) return skip(64,"EGL context changed; restart required");
    // All ImGui access happens on the EGL thread. Input hooks only enqueue copies.
    ImGuiContext* previous = ImGui::GetCurrentContext();
    if (!r.gui) {
        diagnostics::log("RENDER ImGui initialization begin");
        IMGUI_CHECKVERSION();
        r.gui = ImGui::CreateContext();
        r.context = current;
        r.surface = surface;
        ImGuiIO& io = ImGui::GetIO();
        io.IniFilename = nullptr;
        ImFontConfig font;
        font.FontDataOwnedByAtlas = false;
        io.Fonts->AddFontFromMemoryTTF((void*)Roboto_Regular, sizeof(Roboto_Regular), 19.f,
                                     &font, io.Fonts->GetGlyphRangesCyrillic());
        ImGui::StyleColorsDark();
        if (!ImGui_ImplOpenGL3_Init("#version 300 es")) {
            diagnostics::log("RENDER OpenGL backend initialization failed");
            r.failed = true;
            ImGui::DestroyContext(r.gui); r.gui = nullptr;
            ImGui::SetCurrentContext(previous);
            return originalSwap(display, surface);
        }
        r.last = std::chrono::steady_clock::now();
        diagnostics::log("RENDER initialized %dx%d",width,height);
    }
    ImGui::SetCurrentContext(r.gui);
    ImGuiIO& io = ImGui::GetIO();
    auto now = std::chrono::steady_clock::now();
    float dt = std::chrono::duration<float>(now-r.last).count(); r.last = now;
    io.DeltaTime = dt > 0.f && dt < 0.2f ? dt : 1.f/60.f;
    io.DisplaySize = ImVec2(static_cast<float>(width), static_cast<float>(height));
    input::pump();
    ImGui_ImplOpenGL3_NewFrame();
    ImGui::NewFrame();
    menu::render();
    if (!inputReady.load()) {
        ImGui::Begin("Input unavailable");
        ImGui::TextUnformatted("No compatible touch hook. Check logcat / Android version.");
        ImGui::End();
    }
    ImGui::Render();
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
    if (!sawFrame.exchange(true)) diagnostics::log("RENDER first ImGui frame submitted");
    ImGui::SetCurrentContext(previous);
    return originalSwap(display, surface);
}
bool spawn(const char* name,void* (*function)(void*)) {
    diagnostics::log("BOOT creating %s worker",name);
    pthread_t thread;
    const int result=pthread_create(&thread,nullptr,function,nullptr);
    if (result) {diagnostics::log("BOOT %s pthread_create failed rc=%d",name,result);return false;}
    const int detached=pthread_detach(thread);
    diagnostics::log("BOOT %s worker created; detach rc=%d",name,detached);
    return true;
}
void* gameStart(void*) {
    pthread_setname_np(pthread_self(),"EncoreGame");
    diagnostics::log("GAME worker entered");
    runtime::installGameHooks();
    diagnostics::log("GAME install returned: %s",runtime::status());
    return nullptr;
}
void* inputStart(void*) {
    pthread_setname_np(pthread_self(),"EncoreInput");
    diagnostics::log("INPUT worker entered");
    const bool ready=input::install();inputReady.store(ready);
    diagnostics::log("INPUT install returned ready=%d",ready);
    return nullptr;
}
void* start(void*) {
    pthread_setname_np(pthread_self(),"EncoreBoot");
    diagnostics::log("BOOT worker entered; opening libEGL.so");
    // Keep the handle open for the lifetime of installed function hooks.
    void* egl=dlopen("libEGL.so",RTLD_NOW|RTLD_LOCAL);
    if (!egl) { const char* error=dlerror();diagnostics::log("BOOT EGL dlopen failed: %s",error?error:"unknown");return nullptr; }
    diagnostics::log("BOOT EGL opened; resolving eglSwapBuffers");
    void* address=dlsym(egl,"eglSwapBuffers");
    if (!address) {diagnostics::log("BOOT eglSwapBuffers symbol unavailable");return nullptr;}
    if (hooks::install(address,reinterpret_cast<void*>(swap),reinterpret_cast<void**>(&originalSwap),"eglSwapBuffers")!=0) {
        diagnostics::log("BOOT EGL hook failed; no renderer installed");return nullptr;
    }
    // Input symbol resolution must not prevent the independent game bootstrap from running.
    spawn("game",gameStart);
    spawn("input",inputStart);
    diagnostics::log("BOOT EGL hook installed; waiting for GLES3 callback (Vulkan is not supported)");
    return nullptr;
}
__attribute__((constructor)) void loaded() {
    diagnostics::log("BOOT constructor entered");
    diagnostics::begin();
    // Root dlopen does not call JNI_OnLoad. No Java VM or external overlay required.
    char name[256]{};
    FILE* file=std::fopen("/proc/self/cmdline","rb");
    if (!file) {diagnostics::log("BOOT cmdline open failed errno=%d",errno);return;}
    const size_t count=std::fread(name,1,sizeof(name)-1,file);
    const int error=std::ferror(file)?errno:0;std::fclose(file);
    diagnostics::log("BOOT cmdline=%s bytes=%zu read_errno=%d",name,count,error);
    if (std::strcmp(name,target::Package)!=0) {diagnostics::log("BOOT stopped: target package mismatch");return;}
    spawn("bootstrap",start);
    diagnostics::log("BOOT constructor returning");
}
}
