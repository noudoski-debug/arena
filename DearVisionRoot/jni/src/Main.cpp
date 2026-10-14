#include "imgui.h"
#include "imgui_impl_opengl3.h"
#include "Roboto-Regular.h"
#include "dobby.h"
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
std::atomic<bool> inputReady{false};
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
    auto& r = renderer();
    std::unique_lock<std::mutex> lock(r.mutex, std::try_to_lock);
    EGLint width = 0, height = 0, version = 0;
    const EGLContext current = eglGetCurrentContext();
    if (!lock.owns_lock() || r.failed || current == EGL_NO_CONTEXT ||
        !eglQueryContext(display, current, EGL_CONTEXT_CLIENT_VERSION, &version) || version < 3 ||
        !eglQuerySurface(display, surface, EGL_WIDTH, &width) ||
        !eglQuerySurface(display, surface, EGL_HEIGHT, &height) || width < 100 || height < 100 ||
        (r.gui && r.context != current))
        return originalSwap(display, surface);
    // All ImGui access happens on the EGL thread. Input hooks only enqueue copies.
    ImGuiContext* previous = ImGui::GetCurrentContext();
    if (!r.gui) {
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
            r.failed = true;
            ImGui::DestroyContext(r.gui); r.gui = nullptr;
            ImGui::SetCurrentContext(previous);
            return originalSwap(display, surface);
        }
        r.last = std::chrono::steady_clock::now();
        __android_log_print(ANDROID_LOG_INFO, "DearVision", "Internal renderer initialized: %dx%d", width, height);
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
    ImGui::SetCurrentContext(previous);
    return originalSwap(display, surface);
}
void* start(void*) {
    // Keep the handle open for the lifetime of installed function hooks.
    void* egl = dlopen("libEGL.so", RTLD_NOW | RTLD_LOCAL);
    void* address = egl ? dlsym(egl, "eglSwapBuffers") : nullptr;
    if (!address || DobbyHook(address, reinterpret_cast<void*>(swap), reinterpret_cast<void**>(&originalSwap)) != 0) {
        __android_log_print(ANDROID_LOG_ERROR, "DearVision", "eglSwapBuffers hook failed; no renderer installed");
        return nullptr;
    }
    inputReady.store(input::install());
    runtime::installGameHooks();
    __android_log_print(ANDROID_LOG_INFO, "DearVision", "OpenGL ES required. Vulkan is NOT patched or disabled.");
    return nullptr;
}
__attribute__((constructor)) void loaded() {
    // Root dlopen does not call JNI_OnLoad. No Java VM or external overlay required.
    char name[256]{};
    FILE* f = std::fopen("/proc/self/cmdline", "rb");
    if (!f) return;
    std::fread(name, 1, sizeof(name)-1, f); std::fclose(f);
    if (std::strcmp(name, target::Package) != 0) return;
    pthread_t thread;
    const int error = pthread_create(&thread, nullptr, start, nullptr);
    if (error == 0) pthread_detach(thread);
    else __android_log_print(ANDROID_LOG_ERROR, "DearVision", "pthread_create failed: %d", error);
}
}
