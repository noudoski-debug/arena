#include "Input.h"
#include "imgui.h"
#include "dobby.h"
#include <android/input.h>
#include <android/log.h>
#include <dlfcn.h>
#include <deque>
#include <array>
#include <algorithm>
#include <atomic>
#include <mutex>

namespace input {
namespace {
struct Event { float x, y; int button; }; // -1: move, 0: release, 1: press
struct Queue { std::mutex mutex; std::deque<Event> events; std::vector<Touch> touches;
    std::array<std::atomic<bool>,768> keys{}; std::atomic<bool> toggle{false}; };
Queue& queue() { static Queue q; return q; }
using InitializeMotion = void (*)(void*, void*, void*);
using Consume = int32_t (*)(void*, void*, bool, int64_t, uint32_t*, void**);
InitializeMotion originalInit = nullptr;
Consume originalConsume = nullptr;
void capture(AInputEvent* event) {
    if (!event) return;
    auto& state=queue();
    if (AInputEvent_getType(event)==AINPUT_EVENT_TYPE_KEY) {
        int code=AKeyEvent_getScanCode(event);
        int action=AKeyEvent_getAction(event);
        if (code>0 && code<(int)state.keys.size()) {
            bool down=action!=AKEY_EVENT_ACTION_UP;
            bool old=state.keys[code].exchange(down);
            if (down && !old && (code==68 || code==110)) state.toggle.store(true);
        }
        return;
    }
    if (AInputEvent_getType(event)!=AINPUT_EVENT_TYPE_MOTION || AMotionEvent_getPointerCount(event)==0) return;
    int full=AMotionEvent_getAction(event);
    int kind=full & AMOTION_EVENT_ACTION_MASK;
    int index=(full & AMOTION_EVENT_ACTION_POINTER_INDEX_MASK)>>AMOTION_EVENT_ACTION_POINTER_INDEX_SHIFT;
    {
        std::lock_guard<std::mutex> lock(state.mutex);
        state.touches.clear();
        if (kind!=AMOTION_EVENT_ACTION_UP && kind!=AMOTION_EVENT_ACTION_CANCEL) {
            size_t count=std::min<size_t>(AMotionEvent_getPointerCount(event),10);
            for(size_t i=0;i<count;++i) {
                if(kind==AMOTION_EVENT_ACTION_POINTER_UP && (int)i==index) continue;
                state.touches.push_back({AMotionEvent_getPointerId(event,i),AMotionEvent_getX(event,i),AMotionEvent_getY(event,i)});
            }
        }
    }
    int buttons=AMotionEvent_getButtonState(event);
    state.keys[272].store((buttons & AMOTION_EVENT_BUTTON_PRIMARY)!=0);
    state.keys[273].store((buttons & AMOTION_EVENT_BUTTON_SECONDARY)!=0);
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
    // Prefer consume: it receives keyboard AND motion events. Fallback supports touch only.
    void* read = symbol("_ZN7android13InputConsumer7consumeEPNS_26InputEventFactoryInterfaceEblPjPPNS_10InputEventE");
    if (read && DobbyHook(read, reinterpret_cast<void*>(consume), reinterpret_cast<void**>(&originalConsume)) == 0) return true;
    void* init = symbol("_ZN7android13InputConsumer21initializeMotionEventEPNS_11MotionEventEPKNS_12InputMessageE");
    if (init && DobbyHook(init, reinterpret_cast<void*>(initialize), reinterpret_cast<void**>(&originalInit)) == 0) return true;
    __android_log_print(ANDROID_LOG_ERROR, "EncoreDLC", "Input hook unavailable on this Android build");
    return false;
}
std::vector<Touch> touches() { auto& q=queue();std::lock_guard<std::mutex> lock(q.mutex);return q.touches; }
bool isDown(int code) { return code<=0 || (code<(int)queue().keys.size() && queue().keys[code].load()); }
bool consumeMenuToggle() { return queue().toggle.exchange(false); }
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
