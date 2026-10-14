// collector.cpp — internal сборщик (Unity 6). БЕЗ il2cpp API (экспортов нет):
// klass берём из кэша/external-подсказки, цепочка klass->iL->Dictionary->игроки.
// Только чтение игры + запись esp.bin. Хуков/патчей нет.
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include <cstdio>
#include <cstring>
#include <cstdint>
#include <cmath>
#include <unistd.h>
#include <pthread.h>
#include <sys/stat.h>

#include "SharedEsp.h"

namespace {
const uintptr_t kStaticFieldsOff = 0xB8;
const uintptr_t kPosOff = 0x1C8;
const uintptr_t kUserIdOff = 0x278;
const uintptr_t kNetIdOff = 0x40;
const uintptr_t kIsLocalOff = 0x4A;
const uintptr_t kMouseLookOff = 0x70;
const uintptr_t kLookAngleOff = 0x1A8;
const uintptr_t kStrLenOff = 0x10;
const uintptr_t kStrCharsOff = 0x14;
const float kEyeH = 1.6f;
} // namespace

static bool okStr(const void* o, char* out, size_t cap) {
    if (!o || !out || cap == 0) return false;
    uint32_t len = 0;
    memcpy(&len, (const char*)o + kStrLenOff, 4);
    if (len == 0 || len > 64) return false;
    size_t w = 0;
    for (uint32_t i = 0; i < len && w + 4 < cap; i++) {
        uint16_t c = 0;
        memcpy(&c, (const char*)o + kStrCharsOff + i * 2, 2);
        if (c < 0x80) out[w++] = (char)c;
        else if (c < 0x800) {
            out[w++] = (char)(0xC0 | (c >> 6));
            if (w < cap) out[w++] = (char)(0x80 | (c & 0x3F));
        } else {
            out[w++] = (char)(0xE0 | (c >> 12));
            if (w < cap) out[w++] = (char)(0x80 | ((c >> 6) & 0x3F));
            if (w < cap) out[w++] = (char)(0x80 | (c & 0x3F));
        }
    }
    out[w < cap ? w : cap - 1] = 0;
    return true;
}

// та же цепочка что external: iL {8,0,16} x dict {24,16,32} x entries {24,32},{16,24},{32,40}
static bool chain(uint64_t klass, shared67::Frame& fr) {
    void* sf = nullptr;
    memcpy(&sf, (char*)klass + kStaticFieldsOff, 8);
    if (!sf) return false;
    static const uintptr_t kIl[] = {8, 0, 16};
    static const uintptr_t kDict[] = {24, 16, 32};
    static const uintptr_t kEnt[][2] = {{24, 32}, {16, 24}, {32, 40}};
    for (uintptr_t ilOff : kIl) {
        void *il = nullptr, *dict = nullptr;
        memcpy(&il, (char*)sf + ilOff, 8);
        if (!il) continue;
        for (uintptr_t dOff : kDict) {
            memcpy(&dict, (char*)il + dOff, 8);
            if (!dict) continue;
            for (auto& ec : kEnt) {
                void* entries = nullptr;
                uint32_t count = 0;
                memcpy(&entries, (char*)dict + ec[0], 8);
                memcpy(&count, (char*)dict + ec[1], 4);
                if (!entries || count == 0 || count > 64) continue;
                uint64_t arrLen = 0;
                memcpy(&arrLen, (char*)entries + 24, 8);
                if (arrLen < count || arrLen > 512) continue;
                uint32_t out = 0;
                uint64_t k0 = 0;
                float px0 = 0, py0 = 0, pz0 = 0;
                bool bad = false;
                for (uint32_t j = 0; j < count && j < 64; j++) {
                    void* val = nullptr;
                    memcpy(&val, (char*)entries + 32 + (uintptr_t)j * 24 + 16, 8);
                    if (!val) { bad = true; break; }
                    uint64_t k = 0;
                    memcpy(&k, val, 8);
                    if (!k) { bad = true; break; }
                    if (j == 0) k0 = k;
                    else if (k != k0) { bad = true; break; }
                    float p[3] = {0};
                    memcpy(p, (char*)val + kPosOff, 12);
                    if (!std::isfinite(p[0]) || !std::isfinite(p[1]) || !std::isfinite(p[2])) { bad = true; break; }
                    float ol = p[0] * p[0] + p[1] * p[1] + p[2] * p[2];
                    if (ol < 25.f || ol > 90000000.f) { bad = true; break; }
                    if (j == 0) { px0 = p[0]; py0 = p[1]; pz0 = p[2]; }
                    else {
                        float dx = p[0] - px0, dy = p[1] - py0, dz = p[2] - pz0;
                        if (dx * dx + dy * dy + dz * dz > 8000.f * 8000.f) { bad = true; break; }
                    }
                    if (out < 64) {
                        auto& e = fr.ents[out];
                        e.x = p[0]; e.y = p[1]; e.z = p[2];
                        e.yaw = 0; e.pitch = 0; e.isLocal = 0;
                        void* idObj = nullptr;
                        memcpy(&idObj, (char*)val + kUserIdOff, 8);
                        if (idObj) okStr(idObj, e.name, sizeof(e.name));
                        void* ident = nullptr;
                        memcpy(&ident, (char*)val + kNetIdOff, 8);
                        uint8_t loc = 0;
                        if (ident) memcpy(&loc, (char*)ident + kIsLocalOff, 1);
                        e.isLocal = loc ? 1 : 0;
                        float yaw = 0;
                        memcpy(&yaw, (char*)val + kLookAngleOff, 4);
                        if (std::isfinite(yaw) && fabsf(yaw) < 100.f) e.yaw = yaw;
                        out++;
                    }
                }
                if (!bad && out >= 2) {
                    fr.count = out;
                    return true;
                }
            }
        }
    }
    return false;
}

static void* run(void*) {
    // байпасс-база: нейтральное имя треда, низкий приоритет, без логов
    pthread_setname_np(pthread_self(), "AudioDecoder");
    nice(10);
    // подождать прогруз мира/il2cpp
    sleep(8);
    uint32_t seq = 0;
    uint64_t klass = 0;
    // klass ищем сами прямыми чтениями (dlsym пуст — экспортов нет):
    // строка "PlayerManager" -> указатель -> klass=ptr-16, ns Oxide, цепочка с игроками
    {
        struct MR { uintptr_t s, e; bool w, anon; };
        static MR regs[2048];
        int nregs = 0;
        if (FILE* f = fopen("/proc/self/maps", "r")) {
            char line[1024];
            while (fgets(line, sizeof(line), f) && nregs < 2048) {
                unsigned long s = 0, e = 0;
                char pm[8] = {0};
                if (sscanf(line, "%lx-%lx %7s", &s, &e, pm) != 3) continue;
                if (pm[0] != 'r') continue;
                if (strstr(line, "/dev/") || strstr(line, "dalvik")) continue;
                if (e - s > 256 * 1024 * 1024) continue;
                regs[nregs++] = {(uintptr_t)s, (uintptr_t)e, pm[1] == 'w',
                                 strstr(line, "00:00 0") != nullptr};
            }
            fclose(f);
        }
        const char* target = "PlayerManager";
        static uintptr_t strs[12];
        int nstrs = 0;
        // строки имён — в файлах (либа/метадата), кучу пропускаем
        for (int mi = 0; mi < nregs && nstrs < 12; mi++) {
            if (regs[mi].anon) continue;
            if (regs[mi].anon) continue;
            uintptr_t base = regs[mi].s, len = regs[mi].e - regs[mi].s;
            uintptr_t off = 0;
            while (off + 14 <= len && nstrs < 256) {
                void* f = memmem((void*)(base + off), len - off, target, 14);
                if (!f) break;
                strs[nstrs++] = (uintptr_t)f;
                off = (uintptr_t)f - base + 1;
            }
        }
        for (int si = 0; si < nstrs && !klass; si++) {
            uintptr_t s = strs[si];
            for (int mi = 0; mi < nregs && !klass; mi++) {
                // klass — в anon-куче или данных либы; код и большие файлы скипаем
                if (!regs[mi].anon) {
                    uintptr_t ml = regs[mi].e - regs[mi].s;
                    if (ml > 64 * 1024 * 1024) continue;
                }
                uintptr_t scanned = 0;
                for (uintptr_t p = regs[mi].s; p + 8 <= regs[mi].e; p += 8) {
                    if ((++scanned & 0x1FFFFF) == 0) usleep(2000); // не вешаем игру
                    if (*(volatile uint64_t*)p != s) continue;
                    uint64_t kc = p - 16;
                    if (kc & 7) continue;
                    if (*(volatile uint64_t*)(kc + 16) != s) continue;
                    const char* nsp = (const char*)*(volatile uint64_t*)(kc + 24);
                    if (!nsp || strcmp(nsp, "Oxide") != 0) continue;
                    if (!*(volatile uint64_t*)kc) continue;
                    shared67::Frame probe{};
                    if (chain(kc, probe) && probe.count >= 2) { klass = kc; break; }
                }
            }
        }
    }
    for (;;) {
        shared67::Frame fr{};
        fr.magic = shared67::kMagic;
        fr.version = shared67::kVersion;
        fr.seq = ++seq;
        if (klass && chain(klass, fr) && fr.count > 0) {
            FILE* f = fopen(shared67::kTmpPath, "wb");
            if (f) {
                fwrite(&fr, 1, sizeof(fr), f);
                fclose(f);
                rename(shared67::kTmpPath, shared67::kPath);
                chmod(shared67::kPath, 0666);
            }
        }
        usleep(50000);
    }
    return nullptr;
}

__attribute__((constructor)) static void onLoad() {
    pthread_t th;
    pthread_attr_t at;
    pthread_attr_init(&at);
    pthread_attr_setdetachstate(&at, PTHREAD_CREATE_DETACHED);
    pthread_create(&th, &at, run, nullptr);
    pthread_attr_destroy(&at);
}
