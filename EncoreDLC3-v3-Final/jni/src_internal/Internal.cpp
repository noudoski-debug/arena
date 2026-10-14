// Internal.cpp — коллектор внутри игры (Unity 6, metadata 39).
// Загрузка через wrap/LD_PRELOAD. Ничего в игру НЕ пишем, только читаем
// через il2cpp_class_from_name и складываем кадры в esp.bin.
// Имя либы и строки замаскированы минимально; логов в logcat нет.
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include <cstdio>
#include <cstring>
#include <cstdint>
#include <cmath>
#include <unistd.h>
#include <pthread.h>
#include <dlfcn.h>
#include <sys/stat.h>

#include "SharedEsp.h"

// Оффсеты сверены с dump 1.14.11949 arm64-v8a.
// PlayerManager: lastTickPosition +0x1E8, userID +0x298, netIdentity +0x40, mouseLook +0x70.
// NetworkIdentity.isLocalPlayer находится по +0x4A; MouseLook qxM backing field — +0x88.
namespace {
const uintptr_t kStaticFieldsOff = 0xB8;
const uintptr_t kClientListOff = 0x10;
const uintptr_t kListItemsOff = 0x10;
const uintptr_t kListSizeOff = 0x18;
const uintptr_t kArrLenOff = 24;
const uintptr_t kArrItemsOff = 32;
const uintptr_t kPosOff = 0x1E8;
const uintptr_t kUserIdOff = 0x298;
const uintptr_t kNetIdOff = 0x40;
const uintptr_t kIsLocalOff = 0x4A;
const uintptr_t kMouseLookOff = 0x70;
const uintptr_t kLookOff = 0x88;
const uintptr_t kStrLenOff = 0x10;
const uintptr_t kStrCharsOff = 0x14;
} // namespace

// Минимальные прототипы il2cpp (стабильны в Unity 6).
struct Il2CppImage;
struct Il2CppAssembly;
struct Il2CppClass;
struct Il2CppDomain;
typedef const void* Il2CppMetadataImageHandle;
typedef void* (*Il2CppDomainGetFunc)();
typedef const Il2CppAssembly** (*Il2CppDomainGetAssembliesFunc)(void* domain, size_t* size);
typedef const char* (*Il2CppImageGetNameFunc)(const Il2CppImage* image);
typedef Il2CppClass* (*Il2CppClassFromNameFunc)(const Il2CppImage* image, const char* namespaze, const char* name);

static bool readUtf16to8(const void* strObj, char* out, size_t cap) {
    if (!strObj || !out || cap == 0) return false;
    uint32_t len = 0;
    memcpy(&len, (const char*)strObj + kStrLenOff, 4);
    if (len == 0 || len > 64) { out[0] = 0; return false; }
    size_t o = 0;
    for (uint32_t i = 0; i < len && o + 4 < cap; i++) {
        uint16_t c = 0;
        memcpy(&c, (const char*)strObj + kStrCharsOff + i * 2, 2);
        if (c < 0x80) out[o++] = (char)c;
        else if (c < 0x800) {
            out[o++] = (char)(0xC0 | (c >> 6));
            if (o < cap) out[o++] = (char)(0x80 | (c & 0x3F));
        } else {
            out[o++] = (char)(0xE0 | (c >> 12));
            if (o < cap) out[o++] = (char)(0x80 | ((c >> 6) & 0x3F));
            if (o < cap) out[o++] = (char)(0x80 | (c & 0x3F));
        }
    }
    out[o < cap ? o : cap - 1] = 0;
    return true;
}

static void* collectorMain(void*) {
    // Ждём прогруз il2cpp (первые секунды его может не быть).
    Il2CppClassFromNameFunc classFromName = nullptr;
    for (int i = 0; i < 100 && !classFromName; i++) {
        void* h = dlopen("libil2cpp.so", RTLD_NOLOAD | RTLD_NOW);
        if (h) {
            classFromName = (Il2CppClassFromNameFunc)dlsym(h, "il2cpp_class_from_name");
            // handle не закрываем специально
        }
        if (!classFromName) usleep(300000);
    }
    if (!classFromName) return nullptr;

    auto domainGet = (Il2CppDomainGetFunc)dlsym(RTLD_DEFAULT, "il2cpp_domain_get");
    auto domainAsm = (Il2CppDomainGetAssembliesFunc)dlsym(RTLD_DEFAULT, "il2cpp_domain_get_assemblies");
    auto imageName = (Il2CppImageGetNameFunc)dlsym(RTLD_DEFAULT, "il2cpp_image_get_name");
    if (!domainGet || !domainAsm || !imageName) return nullptr;

    // Ждём Assembly-CSharp и класс Oxide.PlayerManager.
    Il2CppClass* pmKlass = nullptr;
    for (int i = 0; i < 200 && !pmKlass; i++) {
        void* dom = domainGet();
        if (dom) {
            size_t n = 0;
            const Il2CppAssembly** asms = domainAsm(dom, &n);
            for (size_t a = 0; asms && a < n; a++) {
                // assembly->image по оффсету 0 (Il2CppAssembly: image, token, ...).
                const Il2CppImage* img = nullptr;
                memcpy(&img, asms[a], sizeof(img));
                if (!img) continue;
                const char* iname = imageName(img);
                if (!iname) continue;
                // нужен Assembly-CSharp (там Oxide.PlayerManager)
                if (strcmp(iname, "Assembly-CSharp.dll") != 0 && strcmp(iname, "Assembly-CSharp") != 0) continue;
                Il2CppClass* k = classFromName(img, "Oxide", "PlayerManager");
                if (k) { pmKlass = k; break; }
            }
        }
        if (!pmKlass) usleep(500000);
    }
    if (!pmKlass) return nullptr;

    uint32_t seq = 0;
    for (;;) {
        // static_fields -> clientPlayerList (List<PlayerManager> @ +0x10)
        void* sf = nullptr;
        memcpy(&sf, (char*)pmKlass + kStaticFieldsOff, 8);
        if (sf) {
            void* list = nullptr;
            memcpy(&list, (char*)sf + kClientListOff, 8);
            if (list) {
                void* items = nullptr;
                uint32_t size = 0;
                uint64_t len64 = 0;
                memcpy(&items, (char*)list + kListItemsOff, 8);
                memcpy(&size, (char*)list + kListSizeOff, 4);
                if (items) memcpy(&len64, (char*)items + kArrLenOff, 8);
                if (items && size > 0 && size <= 64 && len64 <= 128) {
                    uint32_t n = size;
                    if (n > 64) n = 64;
                    if ((uint64_t)n > len64) n = (uint32_t)len64;
                    shared67::Frame fr{};
                    fr.magic = shared67::kMagic;
                    fr.version = shared67::kVersion;
                    fr.seq = ++seq;
                    uint32_t out = 0;
                    for (uint32_t i = 0; i < n && out < 64; i++) {
                        void* obj = nullptr;
                        memcpy(&obj, (char*)items + kArrItemsOff + (uintptr_t)i * 8, 8);
                        if (!obj) continue;
                        float px = 0, py = 0, pz = 0;
                        memcpy(&px, (char*)obj + kPosOff, 4);
                        memcpy(&py, (char*)obj + kPosOff + 4, 4);
                        memcpy(&pz, (char*)obj + kPosOff + 8, 4);
                        if (!std::isfinite(px) || !std::isfinite(py) || !std::isfinite(pz)) continue;
                        if (fabsf(px) > 30000.f || fabsf(py) > 30000.f || fabsf(pz) > 30000.f) continue;
                        auto& e = fr.ents[out];
                        e.x = px; e.y = py; e.z = pz;
                        e.yaw = 0; e.pitch = 0; e.isLocal = 0;
                        void* idObj = nullptr;
                        memcpy(&idObj, (char*)obj + kUserIdOff, 8);
                        if (idObj) readUtf16to8(idObj, e.name, sizeof(e.name));
                        void* ident = nullptr;
                        memcpy(&ident, (char*)obj + kNetIdOff, 8);
                        uint8_t loc = 0;
                        if (ident) memcpy(&loc, (char*)ident + kIsLocalOff, 1);
                        e.isLocal = loc ? 1 : 0;
                        if (loc) {
                            void* ml = nullptr;
                            memcpy(&ml, (char*)obj + kMouseLookOff, 8);
                            if (ml) memcpy(&e.yaw, (char*)ml + kLookOff, 8); // yaw+pitch сразу
                        }
                        out++;
                    }
                    fr.count = out;
                    // атомарно через tmp+rename
                    FILE* f = fopen(shared67::kTmpPath, "wb");
                    if (f) {
                        fwrite(&fr, 1, sizeof(fr), f);
                        fclose(f);
                        rename(shared67::kTmpPath, shared67::kPath);
                        chmod(shared67::kPath, 0666);
                    }
                }
            }
        }
        usleep(50000); // 20 Гц
    }
    return nullptr;
}

__attribute__((constructor)) static void onLoad() {
    pthread_t th;
    pthread_attr_t at;
    pthread_attr_init(&at);
    pthread_attr_setdetachstate(&at, PTHREAD_CREATE_DETACHED);
    pthread_create(&th, &at, collectorMain, nullptr);
    pthread_attr_destroy(&at);
}
