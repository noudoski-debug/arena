#include "Runtime.h"
#include "GameOffsets.h"
#include "dobby.h"
#include <android/log.h>
#include <link.h>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstring>
#include <mutex>
#include <unordered_map>
#include <unistd.h>

namespace runtime {
namespace {
using Clock = std::chrono::steady_clock;
struct Vec3 { float x, y, z; };
using Method = const void*; // IL2CPP's hidden MethodInfo argument.
using Lifecycle = void (*)(void*, Method);
struct Api {
    Vec3 (*project)(void*, Vec3, Method) = nullptr;
    Vec3 (*position)(void*, Method) = nullptr;
    void* (*transform)(void*, Method) = nullptr;
    void* (*camera)(Method) = nullptr;
    int (*width)(Method) = nullptr;
    int (*height)(Method) = nullptr;
    bool (*local)(void*, Method) = nullptr;
    Lifecycle update = nullptr, disable = nullptr;
};
struct TimedBox { Box box; Clock::time_point time; };
struct State {
    Api api;
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
std::string userId(void* player) {
    void* text = field<void*>(player, offsets::UserId);
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
    std::lock_guard<std::mutex> lock(s.mutex);
    s.players.erase(reinterpret_cast<uintptr_t>(player));
}
void onDisable(void* player, Method method) {
    erase(player); // Before the original callback can release references.
    if (state().api.disable) state().api.disable(player, method);
}
void onUpdate(void* player, Method method) {
    auto& s = state();
    if (s.api.update) s.api.update(player, method);
    if (!player || !s.ready.load(std::memory_order_acquire) || !s.enabled.load()) return;
    // Unity calls stay on the game's Update thread. Render never dereferences a player.
    if (s.api.local(player, nullptr) || field<uint8_t>(player, offsets::Respawning) ||
        field<uint8_t>(player, offsets::Kicked)) { erase(player); return; }
    void* camera = s.api.camera(nullptr);
    void* transform = s.api.transform(player, nullptr);
    const int width = s.api.width(nullptr), height = s.api.height(nullptr);
    if (!camera || !transform || width <= 0 || height <= 0) { erase(player); return; }
    Vec3 feet = s.api.position(transform, nullptr);
    Vec3 head{feet.x, feet.y + 1.6f, feet.z}; // Same approximate height as original DearVision.
    const Vec3 fp = s.api.project(camera, feet, nullptr);
    const Vec3 hp = s.api.project(camera, head, nullptr);
    Box box;
    box.id = reinterpret_cast<uintptr_t>(player);
    box.label = userId(player);
    box.projected = fp.z > 0.1f && hp.z > 0.1f && std::isfinite(fp.x) &&
                    std::isfinite(fp.y) && std::isfinite(hp.x) && std::isfinite(hp.y);
    box.feetX = fp.x / width; box.feetY = 1.f - fp.y / height;
    box.headX = hp.x / width; box.headY = 1.f - hp.y / height;
    void* cameraTransform = s.api.transform(camera, nullptr);
    if (cameraTransform) {
        const Vec3 c = s.api.position(cameraTransform, nullptr);
        box.distance = std::sqrt((c.x-feet.x)*(c.x-feet.x)+(c.y-feet.y)*(c.y-feet.y)+(c.z-feet.z)*(c.z-feet.z));
    }
    std::lock_guard<std::mutex> lock(s.mutex);
    if (s.players.size() < 512 || s.players.count(box.id))
        s.players[box.id] = {std::move(box), Clock::now()};
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

Frame snapshot() {
    auto& s = state();
    std::lock_guard<std::mutex> lock(s.mutex);
    Frame frame;
    const auto cutoff = Clock::now() - std::chrono::milliseconds(300);
    for (auto it = s.players.begin(); it != s.players.end();) {
        if (it->second.time < cutoff) it = s.players.erase(it);
        else { frame.boxes.push_back(it->second.box); ++frame.players; ++it; }
    }
    return frame;
}
void setEnabled(bool enabled) {
    auto& s = state();
    s.enabled.store(enabled);
    if (!enabled) { std::lock_guard<std::mutex> lock(s.mutex); s.players.clear(); }
}
const char* status() { return state().message.load(); }
void installGameHooks() {
    auto& s = state();
    uintptr_t update = 0;
    for (int i = 0; i < 120 && !update; ++i) {
        update = resolve(offsets::PlayerUpdate);
        if (!update) sleep(1);
    }
    const uintptr_t disable = resolve(offsets::PlayerOnDisable);
    s.api.project = reinterpret_cast<decltype(s.api.project)>(resolve(offsets::WorldToScreenPoint));
    s.api.position = reinterpret_cast<decltype(s.api.position)>(resolve(offsets::GetPosition));
    s.api.transform = reinterpret_cast<decltype(s.api.transform)>(resolve(offsets::GetTransform));
    s.api.camera = reinterpret_cast<decltype(s.api.camera)>(resolve(offsets::GetMainCamera));
    s.api.local = reinterpret_cast<decltype(s.api.local)>(resolve(offsets::IsLocalPlayer));
    s.api.width = reinterpret_cast<decltype(s.api.width)>(resolve(offsets::GetScreenWidth));
    s.api.height = reinterpret_cast<decltype(s.api.height)>(resolve(offsets::GetScreenHeight));
    if (!update || !disable || !s.api.project || !s.api.position || !s.api.transform ||
        !s.api.camera || !s.api.local || !s.api.width || !s.api.height) {
        s.message.store("ERROR: IL2CPP RVAs unavailable; check exact game build");
    } else if (DobbyHook(reinterpret_cast<void*>(disable), reinterpret_cast<void*>(onDisable),
                         reinterpret_cast<void**>(&s.api.disable)) != 0) {
        s.message.store("ERROR: OnDisable hook failed");
    } else if (DobbyHook(reinterpret_cast<void*>(update), reinterpret_cast<void*>(onUpdate),
                         reinterpret_cast<void**>(&s.api.update)) != 0) {
        s.message.store("ERROR: Update hook failed");
    } else {
        s.ready.store(true, std::memory_order_release);
        s.message.store("Internal hooks ready / enable ESP in Visuals");
    }
    __android_log_print(ANDROID_LOG_INFO, "DearVision", "%s", status());
}
}
