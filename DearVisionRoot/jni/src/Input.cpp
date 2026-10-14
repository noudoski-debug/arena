#include "Input.h"
#include "imgui.h"
#include "dobby.h"
#include <android/input.h>
#include <android/log.h>
#include <dlfcn.h>
#include <deque>
#include <mutex>

namespace input {
namespace {
struct Event { float x, y; int button; }; // -1: move, 0: release, 1: press
struct Queue { std::mutex mutex; std::deque<Event> events; };
Queue& queue() { static Queue q; return q; }
using InitializeMotion = void (*)(void*, void*, void*);
using Consume = int32_t (*)(void*, void*, bool, int64_t, uint32_t*, void**);
InitializeMotion originalInit = nullptr;
Consume originalConsume = nullptr;
void capture(AInputEvent* event) {
    if (!event || AInputEvent_getType(event) != AINPUT_EVENT_TYPE_MOTION ||
        AMotionEvent_getPointerCount(event) == 0) return;
    int action = AMotionEvent_getAction(event) & AMOTION_EVENT_ACTION_MASK;
    int button = -1;
    if (action == AMOTION_EVENT_ACTION_DOWN) button = 1;
    else if (action == AMOTION_EVENT_ACTION_UP || action == AMOTION_EVENT_ACTION_CANCEL) button = 0;
    else if (action != AMOTION_EVENT_ACTION_MOVE) return;
    Event e{AMotionEvent_getX(event, 0), AMotionEvent_getY(event, 0), button};
    auto& q = queue(); std::lock_guard<std::mutex> lock(q.mutex);
    if (q.events.size() >= 256) {
        q.events.clear();
        q.events.push_back({e.x, e.y, 0}); // Release any stale press after overflow.
    }
    if (button == -1 && !q.events.empty() && q.events.back().button == -1) q.events.back() = e;
    else q.events.push_back(e);
}
void initialize(void* consumer, void* event, void* message) {
    originalInit(consumer, event, message);
    capture(static_cast<AInputEvent*>(event));
}
int32_t consume(void* consumer, void* factory, bool batches, int64_t time, uint32_t* seq, void** event) {
    const int32_t result = originalConsume(consumer, factory, batches, time, seq, event);
    if (result == 0 && event) capture(static_cast<AInputEvent*>(*event));
    return result;
}
void* symbol(const char* name) {
    void* result = dlsym(RTLD_DEFAULT, name);
    if (!result) result = DobbySymbolResolver("/system/lib64/libinput.so", name);
    return result;
}
}
bool install() {
    void* init = symbol("_ZN7android13InputConsumer21initializeMotionEventEPNS_11MotionEventEPKNS_12InputMessageE");
    if (init && DobbyHook(init, reinterpret_cast<void*>(initialize), reinterpret_cast<void**>(&originalInit)) == 0) return true;
    void* read = symbol("_ZN7android13InputConsumer7consumeEPNS_26InputEventFactoryInterfaceEblPjPPNS_10InputEventE");
    if (read && DobbyHook(read, reinterpret_cast<void*>(consume), reinterpret_cast<void**>(&originalConsume)) == 0) return true;
    __android_log_print(ANDROID_LOG_ERROR, "DearVision", "Input hook unavailable on this Android build");
    return false;
}
void pump() {
    std::deque<Event> events;
    { auto& q = queue(); std::lock_guard<std::mutex> lock(q.mutex); events.swap(q.events); }
    ImGuiIO& io = ImGui::GetIO();
    for (const auto& e : events) {
        io.AddMousePosEvent(e.x, e.y);
        if (e.button != -1) io.AddMouseButtonEvent(0, e.button == 1);
    }
}
}
