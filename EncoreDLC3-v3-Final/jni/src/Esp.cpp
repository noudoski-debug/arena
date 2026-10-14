// Esp.cpp — внешний ESP (root). структурные оффсеты сверены с dump 1.14.11949 (arm64-v8a).
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include "Esp.h"
#include "Vars.h"
#include "MenuStubs.h"
#include "RemotePatch.h"
#include "SharedEsp.h"
#include <cstddef>
#include "Android_draw/draw.h"
#include "ImGui/imgui.h"

#include <cstdio>
#include <cstring>
#include <cmath>
#include <cerrno>
#include <vector>
#include <algorithm>
#include <map>
#include <set>
#include <functional>
#include <time.h>
#include <thread>
#include <atomic>
#include <mutex>
#include <sys/uio.h>

namespace esp {
namespace {

struct V3f { float x, y, z; };
struct V2f { float x, y; };

// clientPlayerList: ищем klass PlayerManager по имени (скан rodata),
// затем указатель на него в данных -> klass -> +0xB8 static -> +0x10 list.
static const uintptr_t kStaticFieldsOff = 0xB8;   // Il2CppClass::static_fields (64-bit IL2CPP)
static const uintptr_t kKlassNameOff = 16;        // Il2CppClass_1::name
static const uintptr_t kListItemsOff = 0x10;      // List<T>._items
static const uintptr_t kListSizeOff = 0x18;       // List<T>._size
static const uintptr_t kArrLenOff = 24;           // Il2CppArray.max_length
static const uintptr_t kArrItemsOff = 32;         // Il2CppArray.vector
static const uintptr_t kStrLenOff = 0x10;         // String.length
static const uintptr_t kStrCharsOff = 0x14;       // String chars (UTF-16)

// Nickname path (dump 1.14.11949):
//   PlayerManager +0x130 = nicklabel (class oe, MonoBehaviour)
//   nicklabel     +0x38 = nickname   (UnityEngine.UI.Text)
//   Text          +0xE0 = m_Text     (System.String, protected)
// Secondary path (PlayerID text under the name):
//   nicklabel     +0x40 = playerId   (Text) -> +0xE0
static const uintptr_t kNickLabelOff    = 0x130;  // PlayerManager.nicklabel
static const uintptr_t kNickTextOff     = 0x38;   // oe.nickname  (Text)
static const uintptr_t kPlayerIdTextOff = 0x40;   // oe.playerId  (Text)
static const uintptr_t kNickCharTrOff   = 0x30;   // oe.character (Transform)
static const uintptr_t kIsFriendOff     = 0x90;   // oe.isFriend
static const uintptr_t kIsTeammateOff   = 0x91;   // oe.isTeammate
static const uintptr_t kTextMTextOff    = 0xE0;   // UnityEngine.UI.Text.m_Text

// PlayerManager (dump 1.14.11949, arm64-v8a)
static const uintptr_t kIsLocalOff = 0x4A;        // NetworkIdentity.isLocalPlayer
static const uintptr_t kMouseLookOff = 0x70;
static const uintptr_t kPosOff = 0x1E8;           // lastTickPosition
static const uintptr_t kLookAngleOff = 0x1C8;     // PlayerManager.lookAngle (yaw)
static const uintptr_t kClientListOff = 0x10;     // static_fields + 0x10 = clientPlayerList

// Диагностика (TRACE/YAWJUMP/nocam-дампы) — лишние чтения памяти каждый тик.
// По умолчанию ВЫКЛ: включается файл-командой "dbg 1" (см. main.cpp).
bool g_espDebug = false;

static const float kEyeH = 1.6f;
static const float kBoxH = 1.8f;

static int g_pid = -1;
static bool g_needLookReset = false; // pid сменился — сбросить holdYaw локального
static bool g_dropLocal = false;     // klass умер — липкий адрес мог освободиться, перепроверить по флагу
static uint64_t g_sLocalObj = 0;     // sticky local PlayerManager (экспортируется через esp_getLocalPM)
static uintptr_t g_listObj = 0;  // clientPlayerList object (кеш, revalidate)
static uint64_t g_pmKlass = 0;   // PlayerManager klass (dict-цепочка)
static std::atomic<int> g_frames{0};

// Диск-кеш klass: адрес валиден только внутри того же PID. ASLR/allocator
// могут переиспользовать старый адрес под другой объект, поэтому stale-кэш
// нельзя принимать как живой PlayerManager.
static const char* kKlassCachePath = "/data/local/tmp/67/klass.bin";
static const uint32_t kKlassCacheMagic = 0x37364C4Cu; // '67LN'
struct KlassCache { uint32_t magic; int pid; uint64_t klass; };
void saveKlassCache(uint64_t klass) {
    if (!klass || g_pid <= 0) return;
    FILE* f = fopen(kKlassCachePath, "wb");
    if (!f) return;
    KlassCache c{kKlassCacheMagic, g_pid, klass};
    fwrite(&c, 1, sizeof(c), f);
    fclose(f);
}
bool loadKlassCache(uint64_t& klass) {
    klass = 0;
    FILE* f = fopen(kKlassCachePath, "rb");
    if (!f) return false;
    KlassCache c{};
    size_t n = fread(&c, 1, sizeof(c), f);
    fclose(f);
    if (n != sizeof(c) || c.magic != kKlassCacheMagic) return false;
    if (c.pid != g_pid || !c.klass || (c.klass & 7)) return false;
    klass = c.klass;
    return true;
}

bool readPlayersViaDict(uint64_t klass, std::vector<uint64_t>& objs, bool* shapeOk = nullptr);
bool readPlayersViaList(uint64_t klass, std::vector<uint64_t>& objs, bool* shapeOk = nullptr);
bool readLookYawPitch(uint64_t playerObj, float& yaw, float& pitch, int* src = nullptr);

// ---- robust memory helpers (1 syscall на мелочь, fallback pread, батчи) ----
inline bool readU64(uintptr_t addr, uint64_t& out) {
    return rpatch::readU64(g_pid, addr, out);
}
inline bool readU32(uintptr_t addr, uint32_t& out) {
    return rpatch::readU32(g_pid, addr, out);
}
inline bool readF32(uintptr_t addr, float& out) {
    return rpatch::readF32(g_pid, addr, out);
}
inline bool readU8(uintptr_t addr, uint8_t& out) {
    return rpatch::readU8(g_pid, addr, out);
}
bool readV3f(uintptr_t addr, V3f& out) {
    return rpatch::readMem(g_pid, addr, &out, sizeof(out));
}
bool saneVec(const V3f& v) {
    return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z) &&
           fabsf(v.x) < 30000.f && fabsf(v.y) < 30000.f && fabsf(v.z) < 30000.f;
}
// C-строка за 1 syscall (было: цикл по 32 байта = N syscall на каждую проверку имени).
inline bool readCString(uintptr_t addr, char* out, size_t cap) {
    return rpatch::readCString(g_pid, addr, out, cap);
}
// Il2Cpp String за 2 syscall: len + весь UTF-16 блок разом.
// Было: по 1 syscall НА СИМВОЛ (до 64 syscall на имя) — отсюда лаги и пустой ESP.
std::string readIl2cppString(uintptr_t obj) {
    std::string out;
    if (!obj) return out;
    uint32_t len = 0;
    if (!rpatch::readU32(g_pid, obj + kStrLenOff, len) || len == 0 || len > 64) return out;
    // весь chars-блок одним махом (64*2=128 байт max)
    uint8_t raw[128 * 2] = {0};
    if (!rpatch::readMem(g_pid, obj + kStrCharsOff, raw, (size_t)len * 2)) return out;
    out.reserve(len * 2);
    for (uint32_t i = 0; i < len; i++) {
        uint32_t cp = (uint32_t)raw[i * 2] | ((uint32_t)raw[i * 2 + 1] << 8);
        if (cp == 0) break; // ранний \0 — дальше мусор
        if (cp < 0x80) out.push_back((char)cp);
        else if (cp < 0x800) {
            out.push_back((char)(0xC0 | (cp >> 6)));
            out.push_back((char)(0x80 | (cp & 0x3F)));
        } else {
            out.push_back((char)(0xE0 | (cp >> 12)));
            out.push_back((char)(0x80 | ((cp >> 6) & 0x3F)));
            out.push_back((char)(0x80 | (cp & 0x3F)));
        }
    }
    return out;
}
// Батч-чтение ядра PlayerManager одним read: покрывает netIdentity(0x40),
// mouseLook(0x70), lookAngle(0x1C8), pos(0x1E8), userId(0x298).
// Было: 5 отдельных syscall на игрока. Стало: 1.
struct PlayerCore {
    uint64_t netId;      // +0x40
    uint64_t mouseLook;  // +0x70
    uint64_t vitals;     // +0xC8 (null = труп/неинициализирован)
    uint64_t nickLabel;  // +0x130 (oe nicklabel)
    uint64_t teamZbI;    // +0x120 (team zbI -> tl +0x90)
    float lookAngle;     // +0x1C8
    uint8_t respawning;  // +0x228
    V3f pos;             // +0x1E8
    V3f saved;           // +0x1F4
    V3f deathPos;        // +0x200
    int32_t flags;       // +0x270
    uint8_t kicked;      // +0x275
    uint64_t userId;     // +0x298
    // Распаковываем nickname Text через дополнительный батч
    uint64_t nickTextObj;  // nickLabel + kNickTextOff  (UnityEngine.UI.Text)
    uint64_t pidTextObj;   // nickLabel + kPlayerIdTextOff
    uint64_t nickCharTr;   // nickLabel + kNickCharTrOff (Transform)
};
static const uintptr_t kCoreBase = 0x40;
static const size_t kCoreSpan = 0x298 + 8 - 0x40; // 0x260
bool readPlayerCore(uintptr_t obj, PlayerCore& c) {
    uint8_t buf[kCoreSpan];
    if (!rpatch::readMem(g_pid, obj + kCoreBase, buf, sizeof(buf))) return false;
    memset(&c, 0, sizeof(c));
    memcpy(&c.netId, buf + (0x40 - kCoreBase), 8);
    memcpy(&c.mouseLook, buf + (0x70 - kCoreBase), 8);
    memcpy(&c.vitals, buf + (0xC8 - kCoreBase), 8);
    memcpy(&c.nickLabel, buf + (0x130 - kCoreBase), 8);
    memcpy(&c.teamZbI, buf + (0x120 - kCoreBase), 8);
    memcpy(&c.respawning, buf + (0x228 - kCoreBase), 1);
    memcpy(&c.lookAngle, buf + (0x1C8 - kCoreBase), 4);
    memcpy(&c.pos, buf + (0x1E8 - kCoreBase), sizeof(V3f));
    memcpy(&c.saved, buf + (0x1F4 - kCoreBase), sizeof(V3f));
    memcpy(&c.deathPos, buf + (0x200 - kCoreBase), sizeof(V3f));
    memcpy(&c.flags, buf + (0x270 - kCoreBase), 4);
    memcpy(&c.kicked, buf + (0x275 - kCoreBase), 1);
    memcpy(&c.userId, buf + (0x298 - kCoreBase), 8);
    // Следуем за nickLabel -> nickname Text / playerId Text / character Transform
    // за один батч на 3 указателя, чтобы не плодить syscall.
    c.nickTextObj = 0; c.pidTextObj = 0; c.nickCharTr = 0;
    if (c.nickLabel && !(c.nickLabel & 7)) {
        uint8_t ib[3 * 8];
        if (rpatch::readMem(g_pid, c.nickLabel + kNickCharTrOff, ib, sizeof(ib))) {
            memcpy(&c.nickCharTr, ib + 0, 8);
            memcpy(&c.nickTextObj, ib + (kNickTextOff - kNickCharTrOff), 8);
            memcpy(&c.pidTextObj, ib + (kPlayerIdTextOff - kNickCharTrOff), 8);
        }
    }
    return true;
}
// Unity Transform -> world pos (external). Managed Transform +0x10 = native TransformData.
// Пробуем стандартные цепочки: td+0x38=matrices, idx=td+0x40, pos=matrices+idx*0x30+0xB0.
// Плюс fallback-перебор указателей td+0x.. как matrices с оффсетами 0x90/0xB0.
bool readTransformPos(uintptr_t transManaged, V3f& out) {
    // Всегда свежий резолв (кеш убран): протухший base давал замороженные
    // позиции и боксы отставали. 3 чтения/игрок/тик оверлей тянет (fps~59).
    uint64_t td = 0;
    if (!readU64(transManaged + 0x10, td) || !td || (td & 7)) return false;
    uint8_t hdr[0x110] = {0};
    if (!rpatch::readMem(g_pid, td, hdr, sizeof(hdr))) return false;
    // Залоченная цепочка: td+0x28 -> base+0x90 = world pos.
    // (0x88 даёт x==y мусор, 0x84 пермутацию, 0x8c мелочь — проверено по логу chain.)
    {
        uint64_t base = 0;
        memcpy(&base, hdr + 0x28, 8);
        if (base && !(base & 7)) {
            V3f p{0,0,0};
            if (rpatch::readMem(g_pid, base + 0x90, &p, 12) && saneVec(p)) {
                float ol = p.x*p.x + p.y*p.y + p.z*p.z;
                // Do not reject valid coordinates on an axis or below sea
                // level: those checks made head anchors disappear near the
                // origin and forced aim back to the box-height fallback.
                if (ol > 0.0001f && ol < 8000.f*8000.f &&
                    p.y > -1000.f && p.y < 10000.f) {
                    out = p;
                    return true;
                }
            }
        }
    }
    return false;
}
inline bool readPlayerPosViaTransform(uintptr_t playerObj, V3f& out) {
    uint64_t tr = 0;
    // сначала characterModel->Transform? У нас worldCameraRoot уже Transform.
    // пробуем worldCameraRoot 0x68
    if (readU64(playerObj + 0x68, tr) && tr && !(tr & 7)) {
        if (readTransformPos(tr, out)) return true;
    }
    return false;
}
bool klassNameIs(uintptr_t obj, const char* want) {
    uint64_t klass = 0;
    if (!readU64(obj, klass) || !klass) return false;
    uint64_t namePtr = 0;
    if (!readU64(klass + kKlassNameOff, namePtr) || !namePtr) return false;
    char name[64] = {0};
    if (!readCString(namePtr, name, sizeof(name))) return false;
    return strcmp(name, want) == 0;
}

struct MapR {
    uintptr_t start, end;
    bool r, w;
    bool anon;
    bool isLib;
};

// Android иногда закрывает /proc/PID/maps, но оставляет тот же файл через
// /proc/PID/task/PID/maps. Используем оба пути во всех сканерах.
static FILE* openMapsFile(int pid, char* path, size_t cap, int* firstErr = nullptr) {
    if (!path || cap < 32 || pid <= 0) return nullptr;
    snprintf(path, cap, "/proc/%d/maps", pid);
    FILE* f = fopen(path, "r");
    int err = errno;
    if (f) {
        int c = fgetc(f);
        if (c != EOF) {
            ungetc(c, f);
            if (firstErr) *firstErr = 0;
            return f;
        }
        fclose(f); // empty/zombie map; try the task view below
    }
    snprintf(path, cap, "/proc/%d/task/%d/maps", pid, pid);
    f = fopen(path, "r");
    if (firstErr) *firstErr = err;
    return f;
}

std::vector<MapR> readMaps() {
    std::vector<MapR> out;
    char path[96];
    int openErr = 0;
    FILE* f = openMapsFile(g_pid, path, sizeof(path), &openErr);
    static int diagPid = -1;
    static bool diagDone = false;
    if (!f) {
        if (diagPid != g_pid) {
            diagPid = g_pid;
            diagDone = false;
        }
        if (!diagDone) {
            diagDone = true;
            printf("[esp] maps open failed pid=%d errno=%d (%s)\n",
                   g_pid, openErr, strerror(openErr));
            fflush(stdout);
        }
        return out;
    }
    unsigned rawLines = 0, parsed = 0, readable = 0, filtered = 0, tooLarge = 0;
    char line[1024];
    while (fgets(line, sizeof(line), f)) {
        rawLines++;
        unsigned long s = 0, e = 0;
        char perms[8] = {0};
        if (sscanf(line, "%lx-%lx %7s", &s, &e, perms) != 3) continue;
        parsed++;
        bool isLib = strstr(line, "libil2cpp.so") != nullptr;
        bool isAnon = strstr(line, " 00:00 0") != nullptr;
        if (perms[0] != 'r') continue;
        readable++;
        if (strstr(line, "/dev/") || strstr(line, "dalvik")) { filtered++; continue; }
        unsigned long mlen = e > s ? e - s : 0;
        // строки/klass могут быть в любом file-mapping (metadata без имени), поэтому
        // мелкие файлы берём все, крупные — только либу/anon/memfd.
        // Лимит 512МБ: klass PlayerManager уже видели в 200МБ+ anon-регионе.
        bool isMemfd = strstr(line, "memfd:") != nullptr;
        if (!isLib && !isAnon && !isMemfd && mlen > 32 * 1024 * 1024) { tooLarge++; continue; }
        if (mlen > 512 * 1024 * 1024 && !isLib) { tooLarge++; continue; }
        out.push_back({(uintptr_t)s, (uintptr_t)e, true, perms[1] == 'w', isAnon && !isLib, isLib});
    }
    fclose(f);
    if (diagPid != g_pid) {
        diagPid = g_pid;
        diagDone = false;
    }
    if (!diagDone || out.empty()) {
        diagDone = true;
        char state[64] = "unknown";
        char statusPath[96];
        snprintf(statusPath, sizeof(statusPath), "/proc/%d/status", g_pid);
        FILE* sf = fopen(statusPath, "r");
        if (sf) {
            char sl[256];
            while (fgets(sl, sizeof(sl), sf)) {
                if (strncmp(sl, "State:", 6) == 0) {
                    snprintf(state, sizeof(state), "%s", sl + 6);
                    break;
                }
            }
            fclose(sf);
        }
        printf("[esp] maps pid=%d path=%s state=%s lines=%u parsed=%u readable=%u filtered=%u large=%u kept=%u\n",
               g_pid, path, state, rawLines, parsed, readable, filtered, tooLarge, (unsigned)out.size());
        fflush(stdout);
    }
    return out;
}

// Толерантный чанк для сканеров: битые страницы зануляем и идём дальше,
// а не рвём весь 1МБ регион (старый код break'ал скан на первой дырке).
inline bool readChunk(uintptr_t addr, uint8_t* out, size_t len, size_t* got = nullptr) {
    return rpatch::readChunkTolerant(g_pid, addr, out, len, got);
}

static int g_dbg = 0;

bool validatePlayerList(uint64_t klass) {
    // быстрая отбраковка ложных указателей на строку:
    // настоящий Il2CppClass: image != 0, namespaze читаемая ("Oxide"/""), выравнивание
    if (klass & 7) return false;
    uint64_t img = 0, nsPtr = 0;
    if (!readU64(klass, img) || !img) return false;
    if (!readU64(klass + 24, nsPtr)) return false;
    if (nsPtr) {
        char ns[64] = {0};
        if (!readCString(nsPtr, ns, sizeof(ns))) return false;
        // namespace PlayerManager по дампу — Oxide, но не требуем строго,
        // требуем только короткую печатную строку
        size_t nsl = strlen(ns);
        if (nsl > 48) return false;
        for (size_t i = 0; i < nsl; i++)
            if (ns[i] < 32 || ns[i] > 126) return false;
    }
    uint64_t sf = 0, list = 0, items = 0;
    uint32_t size = 0;
    uint64_t len64 = 0;
    if (!readU64(klass + kStaticFieldsOff, sf) || !sf || (sf & 7)) {
        if ((g_dbg++ % 200) == 0) printf("[esp] validate 0x%zx: bad sf=0x%zx\n", (size_t)klass, (size_t)sf);
        return false;
    }
    if (!readU64(sf + 0x10, list) || !list) {
        if ((g_dbg % 200) == 0) printf("[esp] validate 0x%zx: sf=0x%zx no list@+0x10\n", (size_t)klass, (size_t)sf);
        return false;
    }
    if (!klassNameIs(list, "List`1")) {
        char nb[64] = {0};
        uint64_t lk = 0, np = 0;
        if (readU64(list, lk) && lk && readU64(lk + kKlassNameOff, np) && np)
            readCString(np, nb, sizeof(nb));
        if ((g_dbg % 50) == 0) printf("[esp] validate 0x%zx: list=0x%zx not List`1 (is '%s')\n", (size_t)klass, (size_t)list, nb);
        return false;
    }
    if (!readU64(list + kListItemsOff, items) || !items) return false;
    if (!readU64(items + kArrLenOff, len64)) return false;
    if (!readU32(list + kListSizeOff, size)) return false;
    if (size > 5000 || len64 > 5000) return false;
    if (size == 0) {
        // класс правильный, но мир пуст (меню/загрузка) — кешируем klass,
        // список подтянем следующим тиком без полного рескана
        g_listObj = list;
        static bool once = false;
        if (!once) { once = true; printf("[esp] klass OK, list empty, waiting players (list=0x%zx)\n", (size_t)list); fflush(stdout); }
        return false;
    }
    uint64_t item0 = 0;
    if (!readU64(items + kArrItemsOff, item0) || !item0) return false;
    uint64_t ik = 0;
    if (!readU64(item0, ik) || ik != klass) {
        if ((g_dbg % 50) == 0) printf("[esp] validate: item0 klass mismatch 0x%zx vs 0x%zx\n", (size_t)ik, (size_t)klass);
        return false;
    }
    g_listObj = list;
    printf("[esp] clientPlayerList=0x%zx n=%u\n", (size_t)list, size);
    fflush(stdout);
    return true;
}

struct LibSeg { uintptr_t start, end, off; bool x; };

bool getLibSegs(std::vector<LibSeg>& out) {
    char path[64];
    FILE* f = openMapsFile(g_pid, path, sizeof(path));
    if (!f) return false;
    char line[1024];
    while (fgets(line, sizeof(line), f)) {
        if (!strstr(line, "libil2cpp.so")) continue;
        unsigned long s = 0, e = 0, o = 0;
        char perms[8] = {0};
        if (sscanf(line, "%lx-%lx %7s %lx", &s, &e, perms, &o) != 4) continue;
        out.push_back({(uintptr_t)s, (uintptr_t)e, (uintptr_t)o, strstr(perms, "x") != nullptr});
    }
    fclose(f);
    return !out.empty();
}

bool getLibExec(std::vector<std::pair<uintptr_t,uintptr_t>>& out) {
    char path[64];
    FILE* f = openMapsFile(g_pid, path, sizeof(path));
    if (!f) return false;
    char line[1024];
    while (fgets(line, sizeof(line), f)) {
        if (!strstr(line, "libil2cpp.so")) continue;
        if (!strstr(line, "r-xp")) continue;
        unsigned long s = 0, e = 0;
        if (sscanf(line, "%lx-%lx", &s, &e) != 2) continue;
        out.push_back({(uintptr_t)s, (uintptr_t)e});
    }
    fclose(f);
    return !out.empty();
}

// Shape-скан: ищем clientPlayerList по форме, без имён классов.
// List: [0]=klass?, [16]=items, [24]=size(1..64)
// Array: [24]=len(1..128), [32]=item0
// Player: [+0x1E8]=pos sane, [+0x298]=userID string, klass одинаковый у item0/item1.
bool shapeValidPlayer(uint64_t obj, uint64_t& klassOut, bool needName) {
    uint64_t k = 0;
    if (!readU64(obj, k) || !k || (k & 7)) return false;
    float p[3] = {0};
    if (!rpatch::readMem(g_pid, obj + 0x1E8, p, 12)) return false;
    for (int i = 0; i < 3; i++)
        if (!std::isfinite(p[i]) || fabsf(p[i]) > 30000.f) return false;
    // меню-болванка в нулях — не игрок в мире
    if (p[0] == 0.f && p[1] == 0.f && p[2] == 0.f) return false;
    if (needName) {
        uint64_t id = 0;
        if (!readU64(obj + 0x298, id) || !id) return false;
        uint32_t len = 0;
        if (!readU32(id + 0x10, len) || len == 0 || len > 32) return false;
    }
    klassOut = k;
    return true;
}


static int g_klassLogged = 0;

// Ищем klass PlayerManager напрямую: указатели на строку "PlayerManager" минус 16,
// Поиск klass по имени (обобщённый; shape==nullptr = без префильтра).
// Кандидат: указатели на строку минус 16, слабая проверка + опциональный shape.
// Oxide — только приоритет (строгий ns-гейт раньше отбраковывал живой klass).
bool findKlassByName(const char* target, bool wantOxide,
                     std::function<bool(uint64_t klass, uint64_t sf)> shape,
                     const std::vector<MapR>& maps, std::vector<uint8_t>& buf,
                     std::vector<uint64_t>& out) {
    out.clear();
    // 1. строки
    std::vector<uintptr_t> strs;
    size_t tlen = strlen(target) + 1;
    const size_t OV = 64;
    for (auto& m : maps) {
        if (!m.r) continue;
        uintptr_t len = m.end - m.start;
        if (len < tlen) continue;
        if (len > 512 * 1024 * 1024) continue;
        for (uintptr_t a = m.start; a < m.end; a += buf.size() - OV) {
            size_t want = buf.size();
            if (a + want > m.end) want = m.end - a;
            size_t got = 0;
            if (!readChunk(a, buf.data(), want, &got) || got < tlen) {
                if (got < tlen) break;
            }
            size_t lim = got >= tlen ? got - tlen + 1 : 0;
            if (a + want < m.end && lim > want - OV) lim = want - OV;
            for (size_t i = 0; i < lim; i++) {
                if (memcmp(buf.data() + i, target, tlen) == 0) strs.push_back(a + i);
            }
            if (got < want) break;
            if (a + want >= m.end) break;
        }
        if (strs.size() > 60) break;
    }
    // 2. указатели на строки -> klass = ptr-16, слабая проверка
    // klass вполне может лежать в большой GC-куче — лимит 512МБ
    static int scanNo = 0;
    scanNo++;
    unsigned rRegs = 0;
    for (auto& m : maps) if (m.r) rRegs++;
    printf("[esp] scan#%d '%s' strs=%u rRegs=%u\n", scanNo, target, (unsigned)strs.size(), rRegs);
    fflush(stdout);
    unsigned ptrHits = 0, nsReject = 0, dupSkip = 0;
    // Search all mappings once for pointers to any matching string. The old
    // implementation rescanned every mapping once per string (up to dozens of
    // full process scans), which was the main cold-start delay.
    std::set<uintptr_t> stringSet(strs.begin(), strs.end());
    for (auto& m : maps) {
        if (!m.r) continue;
        if (m.end - m.start > 512 * 1024 * 1024) continue;
        for (uintptr_t a = m.start; a < m.end; a += buf.size() - OV) {
            size_t want = buf.size();
            if (a + want > m.end) want = m.end - a;
            size_t got = 0;
            if (!readChunk(a, buf.data(), want, &got) || got < 8) {
                if (got < 8) break;
            }
            size_t lim = got >= 8 ? got - 8 + 1 : 0;
            if (a + want < m.end && lim > want - OV) lim = want - OV;
            for (size_t i = 0; i < lim; i += 8) {
                uint64_t v = 0;
                memcpy(&v, buf.data() + i, 8);
                auto sit = stringSet.find((uintptr_t)v);
                if (sit == stringSet.end()) continue;
                uintptr_t stringAddr = *sit;
                uint64_t klass = (a + i) - 16;
                if (klass & 7) continue;
                uint64_t img = 0, sf = 0;
                if (!readU64(klass, img) || !img) continue;
                if (!readU64(klass + 0xB8, sf)) continue;
                // Candidate: klass + 16 must point to the exact matching name.
                uint64_t knp = 0;
                if (!readU64(klass + 16, knp) || knp != stringAddr) continue;
                ptrHits++;
                bool dup = false;
                for (auto k : out) if (k == klass) { dup = true; break; }
                if (dup) { dupSkip++; continue; }
                if (shape && !shape(klass, sf)) continue;
                uint64_t nsp = 0;
                char ns[48] = {0};
                bool oxide = wantOxide && readU64(klass + 24, nsp) && nsp &&
                             readCString(nsp, ns, sizeof(ns)) &&
                             strcmp(ns, "Oxide") == 0;
                if (wantOxide && !oxide) nsReject++;
                if (oxide) out.insert(out.begin(), klass); // Oxide first
                else out.push_back(klass);
                if (g_klassLogged < 12) {
                    g_klassLogged++;
                    printf("[esp] klass cand 0x%zx ns='%s' sf=0x%zx%s\n",
                           (size_t)klass, ns, (size_t)sf, oxide ? " (oxide)" : "");
                    fflush(stdout);
                }
                if (out.size() >= 64) return true;
            }
            if (got < want) break;
            if (a + want >= m.end) break;
        }
    }
    printf("[esp] scan#%d ptrHits=%u nsReject=%u dup=%u klasses=%u\n",
           scanNo, ptrHits, nsReject, dupSkip, (unsigned)out.size());
    fflush(stdout);
    return !out.empty();
}

// Обёртка для PlayerManager (старый вызов из resolveList).
bool findPlayerKlass(const std::vector<MapR>& maps, std::vector<uint8_t>& buf,
                     std::vector<uint64_t>& out) {
    auto listShape = [](uint64_t klass, uint64_t sf) -> bool {
        (void)klass;
        uint64_t list = 0, items = 0;
        uint32_t lsize = 0;
        return readU64(sf + 0x10, list) && list && !(list & 7) &&
               klassNameIs(list, "List`1") &&
               readU64(list + 0x10, items) && items && !(items & 7) &&
               readU32(list + 0x18, lsize) && lsize <= 128;
    };
    return findKlassByName("PlayerManager", true, listShape, maps, buf, out);
}


// Точный адрес метода по file-offset из dump 1.14.11949 arm64-v8a:
// VA = seg.start + (fileOff - seg.off) — только по r-xp сегменту!
bool methodVA(uint64_t fileOff, uintptr_t& va) {
    std::vector<LibSeg> segs;
    if (!getLibSegs(segs)) return false;
    for (auto& s : segs) {
        if (!s.x) continue;
        if (fileOff >= s.off && fileOff < s.off + (s.end - s.start)) {
            va = s.start + (uintptr_t)(fileOff - s.off);
            return true;
        }
    }
    return false;
}

static inline bool inExec(uint64_t v, const std::vector<std::pair<uintptr_t,uintptr_t>>& xr) {
    for (auto& p : xr)
        if (v >= p.first && v < p.second) return true;
    return false;
}

bool ptrInReadable(uintptr_t v, const std::vector<MapR>& maps) {
    for (auto& m : maps) {
        if (!m.r) continue;
        if (v >= m.start && v < m.end) return true;
    }
    return false;
}

// Поиск klass PlayerManager через MethodInfo: *P в exec libil2cpp, klass=*(P+32), имя klass == PlayerManager.
// Не требует знать RVA — работает на любой версии игры.
bool resolveViaMethod(const std::vector<MapR>& maps, std::vector<uint8_t>& buf) {
    std::vector<std::pair<uintptr_t,uintptr_t>> xr;
    if (!getLibExec(xr)) {
        printf("[esp] no libil2cpp exec range\n"); fflush(stdout);
        return false;
    }
    printf("[esp] libil2cpp exec segs=%u", (unsigned)xr.size());
    for (auto& p : xr) printf(" 0x%zx-0x%zx", (size_t)p.first, (size_t)p.second);
    printf("\n"); fflush(stdout);
    const size_t OV = 64;
    unsigned checked = 0, execHits = 0, klassOk = 0;
    for (auto& m : maps) {
        if (!m.r || !m.w) continue; // MethodInfo в heap/rw, r-- даёт 1.8М мусора — скипаем
        if (m.anon && m.end - m.start > 96 * 1024 * 1024) continue;
        for (uintptr_t a = m.start; a < m.end; a += buf.size() - OV) {
            size_t want = buf.size();
            if (a + want > m.end) want = m.end - a;
            size_t got = 0;
            if (!readChunk(a, buf.data(), want, &got) || got < 40) {
                if (got < 40) break;
            }
            size_t lim = got >= 40 ? got - 40 + 1 : 0;
            if (a + want < m.end && lim > want - OV) lim = want - OV;
            for (size_t i = 0; i < lim; i += 8) {
                uint64_t v = 0;
                memcpy(&v, buf.data() + i, 8);
                if (!inExec(v, xr)) continue;
                execHits++;
                if (i + 40 > got) continue;
                uint64_t klassPtr = 0;
                // Unity 6 (metadata 39): methodPointer 0, virtual 8, invoker 16, name 24, klass 32
                memcpy(&klassPtr, buf.data() + i + 32, 8);
                if (!klassPtr || (klassPtr & 7)) continue;
                if (!ptrInReadable(klassPtr, maps)) continue;
                uint64_t namePtr = 0;
                if (!readU64(klassPtr + kKlassNameOff, namePtr) || !namePtr) continue;
                char nm[64] = {0};
                if (!readCString(namePtr, nm, sizeof(nm))) continue;
                // считаем любые читаемые имена чтобы понять что скан вообще находит классы
                klassOk++;
                if ((klassOk % 200000) == 1) { printf("[esp] sample klass '%s'\n", nm); fflush(stdout); }
                if (strcmp(nm, "PlayerManager") != 0) continue;
                checked++;
                printf("[esp] methodinfo hit P=0x%zx klass=0x%zx\n", (size_t)(a + i), (size_t)klassPtr);
                fflush(stdout);
                if (validatePlayerList(klassPtr)) {
                    g_pmKlass = klassPtr;
                    saveKlassCache(klassPtr);
                    return true;
                }
                if (checked > 5) return false; // klass нашли, но список пуст/невалиден — ждём, не спамим
            }
            if (got < want) break;
            if (a + want >= m.end) break;
        }
    }
    printf("[esp] method scan: execHits=%u klassOk=%u checked=%u\n", execHits, klassOk, checked);
    fflush(stdout);
    return false;
}

// Точный поиск: берём file-offset методов PlayerManager из dump 1.14.11949 arm64-v8a,
// считаем runtime VA через maps и ищем MethodInfo с methodPointer == VA.
// Даёт 1-2 кандидата вместо 1.3М — быстро и точно.
bool resolveViaExact(const std::vector<MapR>& maps, std::vector<uint8_t>& buf) {
    // PlayerManager methods from dump 1.14.11949 arm64-v8a (Offset, not RVA).
    // Stable lifecycle methods are used so obfuscation-renamed methods do not
    // invalidate the MethodInfo -> klass resolver on the next dump.
    static const uint64_t kOffs[] = {
        0x69A2114, // .cctor
        0x698EB00, // Update
        0x69AC9B0, // OnStopClient
        0x6985C10, // DeserializeSyncVars
        0x6998994, // OnStartClient
        0x698667C, // OnStartLocalPlayer
    };
    uintptr_t vas[6] = {0};
    unsigned nva = 0;
    for (uint64_t o : kOffs) {
        uintptr_t va = 0;
        if (methodVA(o, va) && va) {
            vas[nva++] = va;
            printf("[esp] exact off 0x%zx -> VA 0x%zx\n", (size_t)o, (size_t)va);
        }
    }
    fflush(stdout);
    if (!nva) { printf("[esp] exact: no VA\n"); fflush(stdout); return false; }
    const size_t OV = 64;
    for (auto& m : maps) {
        if (!m.r) continue; // MethodInfo может быть в r-- relro — смотрим все r
        if (m.end - m.start > 512 * 1024 * 1024) continue; // только гигант режем
        for (uintptr_t a = m.start; a < m.end; a += buf.size() - OV) {
            size_t want = buf.size();
            if (a + want > m.end) want = m.end - a;
            size_t got = 0;
            if (!readChunk(a, buf.data(), want, &got) || got < 32) {
                if (got < 32) break;
            }
            size_t lim = got >= 32 ? got - 32 + 1 : 0;
            if (a + want < m.end && lim > want - OV) lim = want - OV;
            for (size_t i = 0; i < lim; i += 8) {
                uint64_t v = 0;
                memcpy(&v, buf.data() + i, 8);
                bool isTarget = false;
                for (unsigned k = 0; k < nva; k++) if (v == vas[k]) { isTarget = true; break; }
                if (!isTarget) continue;
                if (i + 40 > got) continue;
                uint64_t klassPtr = 0;
                // Unity 6: klass по +32
                memcpy(&klassPtr, buf.data() + i + 32, 8);
                printf("[esp] exact hit P=0x%zx v=0x%zx klass=0x%zx\n",
                       (size_t)(a + i), (size_t)v, (size_t)klassPtr);
                fflush(stdout);
                if (!klassPtr || (klassPtr & 7)) continue;
                uint64_t namePtr = 0;
                if (!readU64(klassPtr + kKlassNameOff, namePtr) || !namePtr) continue;
                char nm[64] = {0};
                if (!readCString(namePtr, nm, sizeof(nm))) continue;
                printf("[esp] exact klass name '%s'\n", nm);
                fflush(stdout);
                if (strcmp(nm, "PlayerManager") != 0) continue;
                if (validatePlayerList(klassPtr)) {
                    g_pmKlass = klassPtr;
                    saveKlassCache(klassPtr);
                    return true;
                }
                return false;
            }
            if (got < want) break;
            if (a + want >= m.end) break;
        }
    }
    printf("[esp] exact: no methodinfo\n"); fflush(stdout);
    return false;
}

bool resolveList() {
    // Кеш с прошлого удачного рана (куча не двигается пока жив pid).
    {
        std::vector<uint64_t> probe;
        bool shape = false;
        if (g_pmKlass) {
            if (readPlayersViaList(g_pmKlass, probe, &shape) && !probe.empty()) return true;
            if (shape) return true; // klass жив, мир пуст (меню) — ждём игроков, не перескан
            if (readPlayersViaDict(g_pmKlass, probe, &shape) && !probe.empty()) return true;
            if (shape) return true;
            // кеш протух — сбросим и пойдём в скан
            g_pmKlass = 0;
        }
    }
    g_pmKlass = 0;
    g_listObj = 0;
    // Диск-кеш klass с прошлого запуска чита (тот же pid игры — адрес тот же).
    // Это правильная версия идеи «указатель из дампа»: хардкод адреса нельзя —
    // ASLR меняет его при каждом рестарте игры, а кеш валидируется shape-проверкой.
    {
        uint64_t cached = 0;
        if (loadKlassCache(cached)) {
            printf("[esp] klass cache try 0x%zx\n", (size_t)cached);
            fflush(stdout);
            std::vector<uint64_t> probe;
            bool shape = false;
            if (readPlayersViaList(cached, probe, &shape) && !probe.empty()) {
                g_pmKlass = cached;
                return true;
            }
            if (shape) {
                printf("[esp] klass cache shape OK 0x%zx\n", (size_t)cached);
                fflush(stdout);
                g_pmKlass = cached;
                return true;
            }
            if (readPlayersViaDict(cached, probe, &shape) && !probe.empty()) {
                g_pmKlass = cached;
                return true;
            }
            if (shape) {
                g_pmKlass = cached;
                return true;
            }
            printf("[esp] klass cache dead\n");
            fflush(stdout);
        }
    }
    auto maps = readMaps();
    std::vector<uint8_t> buf(1024 * 1024);
    if (maps.empty()) {
        printf("[esp] no readable maps for pid=%d; check root/SELinux and target process\n", g_pid);
        fflush(stdout);
        return false;
    }
    // klass-кандидаты: сначала короткий string/metadata путь. Он был самым
    // быстрым стабильным resolver-ом на этой игре; exact scan оставляем только
    // как fallback, чтобы не задерживать первый кадр десятками секунд.
    // берём первого с живой СТРУКТУРОЙ (даже если мир пуст —
    // игроков дождёмся в тике без 5-секундных пересканов).
    {
        std::vector<uint64_t> kl;
        if (findPlayerKlass(maps, buf, kl)) {
            for (uint64_t kc : kl) {
                std::vector<uint64_t> probe;
                bool shape = false;
                // Сначала точный путь из дампа: static_fields + 0x10 = List<PlayerManager>
                if (readPlayersViaList(kc, probe, &shape) && !probe.empty()) {
                    g_pmKlass = kc;
                    saveKlassCache(kc);
                    return true;
                }
                if (shape) {
                    printf("[esp] klass 0x%zx shape OK, waiting live players\n", (size_t)kc);
                    fflush(stdout);
                    g_pmKlass = kc;
                    saveKlassCache(kc);
                    return true;
                }
                if (readPlayersViaDict(kc, probe, &shape) && !probe.empty()) {
                    g_pmKlass = kc;
                    saveKlassCache(kc);
                    return true;
                }
                if (shape) {
                    printf("[esp] klass 0x%zx dict-shape OK, waiting live players\n", (size_t)kc);
                    fflush(stdout);
                    g_pmKlass = kc;
                    saveKlassCache(kc);
                    return true;
                }
            }
            printf("[esp] klasses checked, trying ARM64 MethodInfo offsets\n");
            fflush(stdout);
        }
    }
    // На ARM64 некоторые Unity/IL2CPP-сборки не оставляют имя класса в
    // отображаемых страницах. Только после короткого пути пробуем exact и
    // самый широкий MethodInfo fallback.
    if (resolveViaExact(maps, buf)) return true;
    if (resolveViaMethod(maps, buf)) return true;
    return false;
}

struct Ent {
    V3f pos;
    std::string name;
    bool local = false;
    uint64_t id = 0; // адрес объекта для липкого аима
    bool enemy = true;     // false = тиммейт (iV.isTeammate)
    bool dead = false;     // dead-check: труп/респавн/кик/spectate/death-pos
    bool sleeping = false; // playerFlags Sleeping (отдельный тогл)
    int32_t flags = 0;     // +0x270 сырой (диагностика)
    uint8_t deadCode = 0;  // 1=respawn 2=kick 3=spectate 4=deathPos 5=novitals
    uint64_t teamTl = 0;   // указатель tl команды (сравнение с локальным)
    V3f head{0, 0, 0};     // nickname character Transform (box/root anchor on this build)
    bool haveHead = false;
    V3f characterRoot{0, 0, 0};
    bool haveCharacterRoot = false;
    V3f pivot{0, 0, 0};    // m_LookRoot (MouseLook+0x28), fallback look anchor
    bool havePivot = false;
    V3f headBone{0, 0, 0};  // worldCameraRoot, live head/eye bone anchor
    bool haveHeadBone = false;
    float bodyYaw = 0.0f;   // yaw цели из MouseLook, для боковых костей
    bool haveBodyYaw = false;
    V3f eye{0, 0, 0};      // глаза из Transform (до вычета kEyeH)
    V3f saved{0, 0, 0};    // lastSavedPosition 0x1F4 (диагностика приседа)
    uint8_t tmRaw = 0;     // iV +0x91 сырой (isTeammate?)
    uint8_t respRaw = 0;   // +0x228 сырой (respawning?)
    uint8_t isFriendRaw = 0; // iV +0x90 сырой
    bool visible = true;   // висибл-чек: ноги+голова спроецировались и на экране
};

static std::vector<Ent> s_world;
static V3f s_cam{0, 0, 0}, s_fwd{0, 0, 1};
static float s_tanHalf = 1.0f, s_aspect = 1.0f;
static int s_sw = 0, s_sh = 0;
static bool s_haveCam = false;
// ЕДИНСТВЕННАЯ блокировка: между синхронным сбором кадра (в tick) и aimAngles
// (из аим-треда). Раньше тут был воркер-детач+копии+трипл-буфер — от этого
// и все лаги/дёрганья.
static std::mutex s_snapshotMu;
// Простой кэш рендера: собираем ПОД мьютексом не чаще 30 раз/сек, остальные
// кадры рисуем то что есть. Никаких потоков.
static std::vector<Ent> s_renderWorld;
static V3f s_renderCam{0,0,0}, s_renderFwd{0,0,1};
static float s_renderTan = 1.0f, s_renderAspect = 1.0f;
static int s_renderW = 0, s_renderH = 0;
static bool s_renderHave = false;
static double s_lastCollect = 0.0;
static std::atomic<bool> s_scoped{false};
static float s_gameFov = 0;
struct ArrowSt { float dx = 0, dy = 0; int miss = 0; };
static std::map<uint64_t, ArrowSt> s_arrows;
static float s_lookDbg[2] = {0, 0};
static int sDbgPass = 0;
static int sDbgFlagLocal = 0;

bool readEnt(uintptr_t obj, Ent& e) {
    e.id = obj;
    // 1 батч вместо 4 точечных чтений (pos/userId/netId/mouseLook за раз)
    PlayerCore c{};
    if (!readPlayerCore(obj, c)) return false;
    // lastTickPosition мёртв на этом сервере (всегда 0) — тянем через Transform.
    // Transform worldCameraRoot = ГЛАЗА (камера), поэтому для ног вычитаем eye.
    V3f p = c.pos;
    float ol = p.x*p.x + p.y*p.y + p.z*p.z;
    if (!saneVec(p) || ol < 25.f) {
        V3f tp{0,0,0};
        bool tok = readPlayerPosViaTransform(obj, tp) && saneVec(tp);
        float ol2 = tok ? tp.x*tp.x + tp.y*tp.y + tp.z*tp.z : 0.f;
        if (tok && ol2 >= 25.f) {
            p = tp;
            p.y -= kEyeH; // eye -> feet, дальше код ждёт ноги (cam+1.6, box+1.8, aim+boneH)
            e.eye = tp;
        } else if (saneVec(c.saved) &&
                   (c.saved.x*c.saved.x + c.saved.y*c.saved.y + c.saved.z*c.saved.z) >= 25.f) {
            // Transform не резолвнулся (другая поза/модель) — берём lastSavedPosition.
            // Иначе игрок вообще без ESP ("на некоторых его нет").
            p = c.saved;
            e.eye = {p.x, p.y + kEyeH, p.z};
        } else {
            return false;
        }
    } else {
        e.eye = {p.x, p.y, p.z}; // lastTickPosition жив: считаем его глазами
    }
    // lastTickPosition follows the eye/root offset while crouching on this
    // build.  lastSavedPosition is the ground/root anchor; use its vertical
    // coordinate when the two samples belong to the same player location.
    // Keep the current X/Z so a moving player never gets visibly lagged.
    if (saneVec(c.saved)) {
        float dx = c.saved.x - p.x;
        float dz = c.saved.z - p.z;
        float dy = c.saved.y - p.y;
        if (dx * dx + dz * dz < 2.5f * 2.5f && fabsf(dy) < 3.0f)
            p.y = c.saved.y;
    }
    e.saved = c.saved;
    // DEAD-CHECK v2 (dump 1.14.11949): комбинация сигналов, а не один respawning.
    //  respawning +0x228 !=0 -> мёртв
    //  kicked +0x275 !=0 -> вышел/кикнут (объект-зомби в списке)
    //  flags +0x270: bit1 Spectating -> наблюдатель (не тело в мире)
    //  deathPos +0x200 совпала с pos (<1м) -> только что умер, тело на точке смерти
    //  vitals +0xC8 == 0 -> нет vitals (неинициализирован/труп)
    //  Sleeping (bit0) пишем отдельно в e.sleeping — скипается своим тоглом.
    e.dead = false;
    e.sleeping = false;
    e.flags = c.flags;
    e.deadCode = 0;
    if (c.respawning) { e.dead = true; e.deadCode = 1; }
    else if (c.kicked) { e.dead = true; e.deadCode = 2; }
    else if (c.flags & 2) { e.dead = true; e.deadCode = 3; } // Spectating
    // vitals==0 НЕ считаем трупом: у дальних/только застримленных игроков
    // указатель может быть нулевым, а тело живое — иначе пропадает ESP
    // "на некоторых". Мёртвых ловим по respawning/kicked/spectate/deathPos.
    else {
        float dol = c.deathPos.x * c.deathPos.x + c.deathPos.y * c.deathPos.y +
                    c.deathPos.z * c.deathPos.z;
        if (dol > 25.f) {
            float dx = c.deathPos.x - p.x, dy = c.deathPos.y - p.y, dz = c.deathPos.z - p.z;
            if (dx * dx + dy * dy + dz * dz < 1.0f) { e.dead = true; e.deadCode = 4; }
        }
    }
    if (c.flags & 1) e.sleeping = true; // Sleeping — тело спит (лут), аим скипает по тоглу
    // ESP трупы не рисует никогда (как раньше); аим фильтрует по тоглу в aimAngles.
    // readEnt возвращает true чтобы тоглы в меню реально что-то переключали,
    // фильтрация — в tick()/aimAngles(), а не здесь.
    e.pos = p;
    e.name.clear();
    // имена кешируем по obj (не меняются за жизнь объекта) — 1 промах на игрока, потом из кэша.
    if (c.userId || c.nickTextObj || c.pidTextObj) {
        static uint64_t nameObj[128] = {0};
        static std::string nameCache[128];
        static uint32_t namePos = 0;
        bool hit = false;
        for (int i = 0; i < 128; i++) {
            if (nameObj[i] == obj && !nameCache[i].empty()) { e.name = nameCache[i]; hit = true; break; }
        }
        if (!hit) {
            std::string nick;
            // 1) Nickname UI.Text.m_Text — реальный ник над головой.
            if (c.nickTextObj && !(c.nickTextObj & 7)) {
                uint64_t ns = 0;
                if (readU64(c.nickTextObj + kTextMTextOff, ns) && ns && !(ns & 7))
                    nick = readIl2cppString(ns);
            }
            // 2) Фолбэк: playerId Text (часто содержит уникальный ID или клантег).
            if (nick.empty() && c.pidTextObj && !(c.pidTextObj & 7)) {
                uint64_t ps = 0;
                if (readU64(c.pidTextObj + kTextMTextOff, ps) && ps && !(ps & 7))
                    nick = readIl2cppString(ps);
            }
            // 3) userID строка (обычно шестнадцатеричный ID или steam/guid).
            std::string uidStr;
            if (c.userId && !(c.userId & 7))
                uidStr = readIl2cppString(c.userId);
            // чистим rich-text теги (<color>, <b>, <size>, …) и контрольные символы
            auto clean = [](std::string& s) {
                std::string out;
                out.reserve(s.size());
                bool inTag = false;
                for (char ch : s) {
                    if (ch == '<') { inTag = true; continue; }
                    if (ch == '>') { inTag = false; continue; }
                    if (inTag) continue;
                    if ((unsigned char)ch < 0x20) continue; // контрольные/нулевые
                    out.push_back(ch);
                }
                s.swap(out);
            };
            clean(nick);
            clean(uidStr);
            // Если ник выглядит как плейсхолдер/пусто — подставляем uid; если и он пуст — аноним.
            auto placeholder = [](const std::string& s) {
                if (s.empty()) return true;
                // типичные плейсхолдеры UI
                if (s == "USERNAME" || s == "Player" || s == "..." || s == "Name") return true;
                if (s.size() < 2) return true;
                return false;
            };
            std::string finalName;
            if (!placeholder(nick))      finalName = nick;
            else if (!uidStr.empty())   finalName = uidStr;
            else                         finalName = "Player";
            // Ограничим длину чтобы в pil-е не разъезжалось (длинные UID).
            if (finalName.size() > 22) { finalName.resize(22); finalName += "…"; }
            e.name = finalName;
            nameObj[namePos % 128] = obj;
            nameCache[namePos % 128] = e.name;
            namePos++;
        }
    }
    uint8_t loc = 0;
    if (c.netId && !(c.netId & 7))
        readU8(c.netId + kIsLocalOff, loc);
    e.local = (loc != 0);
    // Команда + голова/корень через nicklabel (oe):
    //   +0x91 isTeammate, +0x90 isFriend,
    //   +0x30 character Transform (корень модели/ника, не кость головы).
    e.enemy = true;
    e.teamTl = 0;
    e.haveHead = false;
    e.haveCharacterRoot = false;
    e.tmRaw = 0;
    e.isFriendRaw = 0;
    e.respRaw = c.respawning;
    if (c.nickLabel && !(c.nickLabel & 7)) {
        uint8_t tm = 0, fr = 0;
        if (readU8(c.nickLabel + kIsTeammateOff, tm)) e.tmRaw = tm;
        if (readU8(c.nickLabel + kIsFriendOff, fr))   e.isFriendRaw = fr;
        if (tm) e.enemy = false;
        uint64_t chTr = c.nickCharTr;
        if (chTr && !(chTr & 7)) {
            V3f hp{0, 0, 0};
            if (readTransformPos(chTr, hp) && saneVec(hp)) {
                float ol = hp.x*hp.x + hp.y*hp.y + hp.z*hp.z;
                if (ol > 25.f) {
                    e.head = hp;
                    e.haveHead = true;
                    e.characterRoot = hp;
                    e.haveCharacterRoot = true;
                }
            }
        }
    }
    // Пивот головы m_LookRoot (MouseLook+0x28): кандидат в настоящую голову.
    // Диагностика расхождения с якорем — в foe-строке (pivDy).
    e.havePivot = false;
    e.haveBodyYaw = false;
    if (c.mouseLook && !(c.mouseLook & 7)) {
        uint64_t lookRoot = 0;
        if (readU64(c.mouseLook + 0x28, lookRoot) && lookRoot && !(lookRoot & 7)) {
            V3f pp{0, 0, 0};
            if (readTransformPos(lookRoot, pp) && saneVec(pp)) {
                float ol = pp.x*pp.x + pp.y*pp.y + pp.z*pp.z;
                if (ol > 25.f) { e.pivot = pp; e.havePivot = true; }
            }
        }
        // MouseLook stores the accumulated yaw at Kzd.y (+0x50), exposed
        // here as the second float of the Vector2 at +0x4C.  It lets the
        // arm/leg bone estimates follow the target's facing direction instead
        // of placing every selectable bone on one vertical line.
        float targetYaw = 0.0f;
        if (readF32(c.mouseLook + 0x4C + 4, targetYaw) &&
            std::isfinite(targetYaw)) {
            e.bodyYaw = targetYaw;
            e.haveBodyYaw = true;
        }
    }
    // PlayerManager.worldCameraRoot (+0x68) is the live camera/eye bone.
    // Read it independently of m_LookRoot: the look root can stay on the
    // character/box root while crouching, whereas the camera root follows the
    // actual head height.  This is the point used by Head aim.
    e.haveHeadBone = false;
    uint64_t cameraRoot = 0;
    if (readU64(obj + 0x68, cameraRoot) && cameraRoot && !(cameraRoot & 7)) {
        V3f cp{0, 0, 0};
        if (readTransformPos(cameraRoot, cp) && saneVec(cp)) {
            e.headBone = cp;
            e.haveHeadBone = true;
            if (!e.havePivot) {
                e.pivot = cp;
                e.havePivot = true;
            }
        }
    }
    // NickLabel.character is the stable character/root transform.  Prefer its
    // Y when it is spatially near the current sample: crouch changes the
    // camera/lastTick Y, but must not move the box's ground anchor.
    if (e.haveCharacterRoot) {
        float dx = e.characterRoot.x - p.x;
        float dz = e.characterRoot.z - p.z;
        float dy = e.characterRoot.y - p.y;
        if (dx * dx + dz * dz < 2.5f * 2.5f && fabsf(dy) < 1.0f)
            p.y = e.characterRoot.y;
    }
    // Keep the corrected root in the published entity used by ESP and Aim.
    e.pos = p;
    // tl команды для сравнения с локальным (в тике): zbI +0x120 -> tl +0x90
    if (c.teamZbI && !(c.teamZbI & 7)) {
        uint64_t tl = 0;
        if (readU64(c.teamZbI + 0x90, tl) && tl && !(tl & 7)) e.teamTl = tl;
    }
    return true;
}

// Точный путь из дампа: klass -> static_fields -> clientPlayerList (List) -> items.
// Работает в соло (хватает 1 живого), нулевые болванки пропускаем, а не бракуем весь список.
// shapeOk=true = структура цела (klass верный), даже если живых нет (меню/загрузка).
bool readPlayersViaList(uint64_t klass, std::vector<uint64_t>& objs, bool* shapeOk) {
    objs.clear();
    if (shapeOk) *shapeOk = false;
    uint64_t sf = 0;
    if (!readU64(klass + kStaticFieldsOff, sf) || !sf || (sf & 7)) return false;
    uint64_t list = 0;
    if (!readU64(sf + kClientListOff, list) || !list || (list & 7)) return false;
    uint64_t items = 0;
    uint32_t size = 0;
    uint64_t len64 = 0;
    if (!readU64(list + kListItemsOff, items) || !items || (items & 7)) return false;
    if (!readU32(list + kListSizeOff, size)) return false;
    if (!readU64(items + kArrLenOff, len64)) return false;
    if (size > 128 || len64 > 512) {
        static int dbgBad = 0;
        if ((dbgBad++ % 30) == 0) {
            printf("[esp] list BAD kc=0x%zx sf=0x%zx list=0x%zx size=%u len=%llu\n",
                   (size_t)klass, (size_t)sf, (size_t)list, size, (unsigned long long)len64);
            fflush(stdout);
        }
        return false;
    }
    if (size == 0) {
        g_listObj = list;
        if (shapeOk) *shapeOk = true; // структура цела, мир пуст (меню)
        return false;
    }
    if ((uint64_t)size > len64) size = (uint32_t)len64;
    uint64_t k0 = 0;
    std::vector<uint64_t> tmp;
    tmp.reserve(size);
    for (uint32_t j = 0; j < size && j < 128; j++) {
        uint64_t val = 0;
        if (!readU64(items + kArrItemsOff + (uintptr_t)j * 8, val) || !val) continue;
        uint64_t k = 0;
        if (!readU64(val, k) || !k) continue;
        if (k0 == 0) k0 = k;
        else if (k != k0) continue; // чужой тип в списке — пропускаем
        tmp.push_back(val); // позицию резолвит readEnt (иначе 2x Transform-чтений на игрока за тик)
    }
    // имя типа проверяем ВСЕГДА (и для пустого tmp) — отличаем верный klass с
    // болванками в нулях от чужого List (String и т.п.)
    if (k0) {
        uint64_t knp = 0;
        char knm[64] = {0};
        if (readU64(k0 + kKlassNameOff, knp) && knp &&
            readCString(knp, knm, sizeof(knm)) &&
            strcmp(knm, "PlayerManager") == 0) {
            if (shapeOk) *shapeOk = true;
            g_listObj = list;
            if (tmp.empty()) {
                static int dbgEmpty = 0;
                if ((dbgEmpty++ % 600) == 0) {
                    // verbose: все айтемы — адрес, klass, pos, userId валидность
                    printf("[esp] list EMPTY live kc=0x%zx size=%u items=0x%zx\n",
                           (size_t)klass, size, (size_t)items);
                    // deep-scan первого объекта: ищем тройки float похожие на координаты
                    {
                        uint64_t v0 = 0;
                        if (readU64(items + kArrItemsOff, v0) && v0) {
                            uint8_t raw[0x300] = {0};
                            if (rpatch::readMem(g_pid, v0, raw, sizeof(raw))) {
                                printf("[esp] deepscan obj=0x%zx floats in +0x60..+0x260:\n", (size_t)v0);
                                for (uintptr_t off = 0x60; off + 12 <= 0x260; off += 4) {
                                    float f0, f1, f2;
                                    memcpy(&f0, raw + off, 4);
                                    memcpy(&f1, raw + off + 4, 4);
                                    memcpy(&f2, raw + off + 8, 4);
                                    if (!std::isfinite(f0) || !std::isfinite(f1) || !std::isfinite(f2)) continue;
                                    if (fabsf(f0) > 8000 || fabsf(f1) > 8000 || fabsf(f2) > 8000) continue;
                                    float ol = f0*f0+f1*f1+f2*f2;
                                    if (ol < 25.f || ol > 8000.f*8000.f) continue;
                                    // пропускаем явный мусор типа yaw-одиночки — нужны 3 живых сразу
                                    printf("[esp]   +0x%zx: %.1f, %.1f, %.1f\n", off, f0, f1, f2);
                                }
                                // указатели рядом с позицией
                                for (uintptr_t off = 0x60; off + 8 <= 0x170; off += 8) {
                                    uint64_t pp = 0;
                                    memcpy(&pp, raw + off, 8);
                                    if (!pp || (pp & 7)) continue;
                                    printf("[esp]   ptr +0x%zx = 0x%zx\n", off, (size_t)pp);
                                }
                                // follow Transform+0x10 (transformData/native) — ищем матрицы с позицией
                    {
                                    uint64_t tr = 0;
                                    memcpy(&tr, raw + 0x68, 8);
                                    uint64_t td = 0;
                                    if (tr && rpatch::readU64(g_pid, tr + 0x10, td) && td && !(td & 7)) {
                                        uint8_t tdr[0x400] = {0};
                                        if (rpatch::readMem(g_pid, td, tdr, sizeof(tdr))) {
                                            printf("[esp]   TransformData obj=0x%zx:\n", (size_t)td);
                                            for (uintptr_t off = 0; off + 8 <= 0x100; off += 8) {
                                                uint64_t q = 0;
                                                memcpy(&q, tdr + off, 8);
                                                if (q > 0x10000 && !(q & 7))
                                                    printf("[esp]     +0x%zx u64=0x%zx\n", off, (size_t)q);
                                            }
                                            for (uintptr_t off = 0; off + 12 <= 0x400; off += 4) {
                                                float f0, f1, f2;
                                                memcpy(&f0, tdr + off, 4);
                                                memcpy(&f1, tdr + off + 4, 4);
                                                memcpy(&f2, tdr + off + 8, 4);
                                                if (!std::isfinite(f0) || !std::isfinite(f1) || !std::isfinite(f2)) continue;
                                                if (fabsf(f0) > 8000 || fabsf(f1) > 8000 || fabsf(f2) > 8000) continue;
                                                float ol = f0*f0+f1*f1+f2*f2;
                                                if (ol < 0.01f || ol > 8000.f*8000.f) continue;
                                                printf("[esp]     +0x%zx: %.2f, %.2f, %.2f\n", off, f0, f1, f2);
                                            }
                                            // второй уровень: +0x38 матрицы?
                                            uint64_t mtx = 0;
                                            memcpy(&mtx, tdr + 0x38, 8);
                                            if (mtx && !(mtx & 7)) {
                                                uint8_t mx[0x200] = {0};
                                                if (rpatch::readMem(g_pid, mtx, mx, sizeof(mx))) {
                                                    printf("[esp]   Matrix obj=0x%zx:\n", (size_t)mtx);
                                                    for (uintptr_t off = 0; off + 12 <= 0x200; off += 16) {
                                                        float f0,f1,f2,f3;
                                                        memcpy(&f0, mx+off, 4); memcpy(&f1, mx+off+4, 4);
                                                        memcpy(&f2, mx+off+8, 4); memcpy(&f3, mx+off+12, 4);
                                                        if (!std::isfinite(f0)||!std::isfinite(f1)||!std::isfinite(f2)||!std::isfinite(f3)) continue;
                                                        if (fabsf(f0)>8000||fabsf(f1)>8000||fabsf(f2)>8000||fabsf(f3)>8000) continue;
                                                        printf("[esp]     +0x%zx: %.2f %.2f %.2f %.2f\n", off, f0,f1,f2,f3);
                                                    }
                                                }
                                            }
                                            fflush(stdout);
                                        }
                                    }
                                }
                                const uintptr_t follows[] = {0x68, 0xA8, 0x148, 0x150};
                                for (uintptr_t fo : follows) {
                                    uint64_t fobj = 0;
                                    memcpy(&fobj, raw + fo, 8);
                                    if (!fobj || (fobj & 7)) continue;
                                    uint8_t fr[0x200] = {0};
                                    if (!rpatch::readMem(g_pid, fobj, fr, sizeof(fr))) {
                                        printf("[esp]   follow +0x%zx obj=0x%zx unread\n", fo, (size_t)fobj);
                                        continue;
                                    }
                                    uint64_t fk = 0;
                                    memcpy(&fk, fr, 8);
                                    uint64_t fnp = 0;
                                    char fn[48] = {0};
                                    if (rpatch::readU64(g_pid, fk + 16, fnp) && fnp)
                                        rpatch::readCString(g_pid, fnp, fn, sizeof(fn));
                                    printf("[esp]   follow +0x%zx obj=0x%zx type='%s':\n", fo, (size_t)fobj, fn);
                                    // raw u64 первые 0x80 чтобы увидеть transformData указатели
                                    for (uintptr_t off = 8; off < 0x80; off += 8) {
                                        uint64_t q = 0;
                                        memcpy(&q, fr + off, 8);
                                        float ff = 0;
                                        memcpy(&ff, fr + off, 4);
                                        if (q > 0x10000)
                                            printf("[esp]     +0x%zx u64=0x%zx f=%.2f\n", off, (size_t)q, ff);
                                    }
                                    for (uintptr_t off = 0; off + 12 <= 0x200; off += 4) {
                                        float f0, f1, f2;
                                        memcpy(&f0, fr + off, 4);
                                        memcpy(&f1, fr + off + 4, 4);
                                        memcpy(&f2, fr + off + 8, 4);
                                        if (!std::isfinite(f0) || !std::isfinite(f1) || !std::isfinite(f2)) continue;
                                        if (fabsf(f0) > 8000 || fabsf(f1) > 8000 || fabsf(f2) > 8000) continue;
                                        float ol = f0*f0+f1*f1+f2*f2;
                                        if (ol < 0.01f || ol > 8000.f*8000.f) continue;
                                        // без фильтра 25 — смотрим даже мелкие
                                        printf("[esp]     +0x%zx: %.2f, %.2f, %.2f\n", off, f0, f1, f2);
                                    }
                                }
                                fflush(stdout);
                            }
                        }
                    }
                    for (uint32_t d = 0; d < size && d < 12; d++) {
                        uint64_t v = 0;
                        if (!readU64(items + kArrItemsOff + (uintptr_t)d * 8, v)) {
                            printf("[esp]  [%u] unreadable ptr\n", d);
                            continue;
                        }
                        if (!v) { printf("[esp]  [%u] null\n", d); continue; }
                        uint64_t kk = 0;
                        if (!readU64(v, kk)) { printf("[esp]  [%u] obj=0x%zx klass unread\n", d, (size_t)v); continue; }
                        PlayerCore cc{};
                        bool ok = readPlayerCore(v, cc);
                        // соседние Vector3: saved 0x1F4, death 0x200 — смотрим что живое
                        V3f ps{0,0,0}, pd{0,0,0};
                        bool okS = readV3f(v + 0x1F4, ps);
                        bool okD = readV3f(v + 0x200, pd);
                        char kn[32] = {0};
                        uint64_t knp2 = 0;
                        if (readU64(kk + kKlassNameOff, knp2) && knp2)
                            readCString(knp2, kn, sizeof(kn));
                        printf("[esp]  [%u] obj=0x%zx k=0x%zx(%s) core=%d pos=%.1f,%.1f,%.1f saved=%.1f,%.1f,%.1f death=%.1f,%.1f,%.1f uid=0x%zx ml=0x%zx yaw=%.2f\n",
                               d, (size_t)v, (size_t)kk, kn, (int)ok,
                               ok ? cc.pos.x : 999, ok ? cc.pos.y : 999, ok ? cc.pos.z : 999,
                               okS ? ps.x : 999, okS ? ps.y : 999, okS ? ps.z : 999,
                               okD ? pd.x : 999, okD ? pd.y : 999, okD ? pd.z : 999,
                               ok ? (size_t)cc.userId : 0, ok ? (size_t)cc.mouseLook : 0,
                               ok ? cc.lookAngle : 999);
                    }
                    fflush(stdout);
                }
                return false;
            }
            objs = tmp;
            // FOUND каждый тик спамил в файл 60 раз/сек и лагал оверлей — только по изменению
            {
                static uint32_t lastN = 0xFFFFFFFF;
                static uint64_t lastList = 0;
                if (tmp.size() != lastN || (uint64_t)list != lastList) {
                    lastN = (uint32_t)tmp.size();
                    lastList = (uint64_t)list;
                    printf("[esp] list FOUND kc=0x%zx list=0x%zx n=%u (size=%u)\n",
                           (size_t)klass, (size_t)list, (unsigned)tmp.size(), size);
                    fflush(stdout);
                }
            }
            return true;
        }
        // тип не тот — чужой список
        static int dbgType = 0;
        if ((dbgType++ % 30) == 0) {
            uint64_t knp = 0;
            char knm[64] = {0};
            if (readU64(k0 + kKlassNameOff, knp) && knp) readCString(knp, knm, sizeof(knm));
            printf("[esp] list WRONGTYPE kc=0x%zx k0type='%s'\n", (size_t)klass, knm);
            fflush(stdout);
        }
    }
    return false;
}

// Взгляд локального (финал v2: оси были перепутаны!):
//   YAW   = Kzd.y @ MouseLook+0x4C+4 (градусы, НЕОГРАНИЧЕН — копится при спине
//           до тысяч; поэтому раньше «прыгало»: смотрели не туда).
//           Kzd.x == lookAngle — это НЕ yaw.
//   PITCH = lookAngle +0x1C8 (градусы, кламп ~±80 — как и положено тангажу).
//           Знак: +lookAngle = вниз (Unity +X), поэтому pitchUp = -lookAngle.
// Kkz +0x88 — дельты мыши, НЕ использовать.
// TRACE-доказательства: спин крутит Kzd.y в тысячи, Kzd.x/lookAngle в ±80;
// горизонт/up/down двигают только Kzd.x? нет — см. коммент ниже.
static float wrap180(float a) {
    a = fmodf(a, 360.f);
    if (a > 180.f) a -= 360.f;
    if (a < -180.f) a += 360.f;
    return a;
}
bool readLookYawPitch(uint64_t playerObj, float& yaw, float& pitch, int* src) {
    if (src) *src = 0;
    static float holdYaw = 0;
    static bool haveHold = false;
    if (g_needLookReset) { haveHold = false; holdYaw = 0; g_needLookReset = false; }
    yaw = haveHold ? holdYaw : 0;
    pitch = 0;
    // pitch: lookAngle всегда живой
    float la = 0;
    bool okLa = readF32(playerObj + kLookAngleOff, la) && std::isfinite(la) && fabsf(la) <= 90.f;
    if (okLa) pitch = -la; // +lookAngle = вниз (Unity +X); знак верный, боксы сидят
    // yaw: Kzd.y; (0,0) НЕ доверяем никогда — это протухший/неинициализированный
    // объект, а не реальный yaw 0 (старый код верил ему при lookAngle~=0 и
    // камера схлопывалась в 0 — ЕСП «плавал»). Держим прошлый yaw.
    uint64_t ml = 0;
    if (readU64(playerObj + kMouseLookOff, ml) && ml && !(ml & 7)) {
        float kzd[2] = {0, 0};
        if (rpatch::readMem(g_pid, ml + 0x4C, kzd, sizeof(kzd)) &&
            std::isfinite(kzd[0]) && std::isfinite(kzd[1])) {
            if (kzd[0] != 0.f || kzd[1] != 0.f) {
                holdYaw = wrap180(kzd[1]);
                haveHold = true;
                yaw = holdYaw;
                if (src) *src = 1;
                return okLa;
            }
        }
    }
    if (src) *src = 2; // yaw холд, pitch свежий
    return okLa;
}

// Цепочка без сканов кучи: klass -> static -> iL(active) -> Dictionary -> entries -> игроки.
// Fallback если прямой List пуст. Болванки пропускаем, хватает 1 живого (соло).
bool readPlayersViaDict(uint64_t klass, std::vector<uint64_t>& objs, bool* shapeOk) {
    objs.clear();
    if (shapeOk) *shapeOk = false;
    uint64_t sf = 0;
    if (!readU64(klass + 0xB8, sf) || !sf || (sf & 7)) return false;
    static const uintptr_t kIl[] = {8, 0, 16};
    static const uintptr_t kDict[] = {24, 16, 32};
    static const uintptr_t kEnt[][2] = {{24, 32}, {16, 24}, {32, 40}};
    for (uintptr_t ilOff : kIl) {
        uint64_t il = 0;
        if (!readU64(sf + ilOff, il) || !il || (il & 7)) continue;
        for (uintptr_t dOff : kDict) {
            uint64_t dict = 0;
            if (!readU64(il + dOff, dict) || !dict || (dict & 7)) continue;
            for (auto& ec : kEnt) {
                uint64_t entries = 0;
                uint32_t count = 0;
                if (!readU64(dict + ec[0], entries) || !entries || (entries & 7)) continue;
                if (!readU32(dict + ec[1], count) || count == 0 || count > 64) continue;
                uint64_t arrLen = 0;
                if (!readU64(entries + 24, arrLen) || arrLen < count || arrLen > 512) continue;
                std::vector<uint64_t> tmp;
                uint64_t k0 = 0;
                V3f p0{0, 0, 0};
                bool haveP0 = false;
                for (uint32_t j = 0; j < count && j < 64; j++) {
                    uint64_t val = 0;
                    if (!readU64(entries + 32 + (uintptr_t)j * 24 + 16, val) || !val) continue;
                    uint64_t k = 0;
                    if (!readU64(val, k) || !k) continue;
                    if (k0 == 0) k0 = k;
                    else if (k != k0) continue;
                    V3f p{0, 0, 0};
                    if (!readV3f(val + 0x1E8, p) || !saneVec(p)) continue;
                    float ol = p.x * p.x + p.y * p.y + p.z * p.z;
                    if (ol < 25.f) continue; // нули-болванки пропускаем
                    if (!haveP0) { p0 = p; haveP0 = true; }
                    else {
                        float dx = p.x - p0.x, dy = p.y - p0.y, dz = p.z - p0.z;
                        if (dx * dx + dy * dy + dz * dz > 8000.f * 8000.f) continue;
                    }
                    tmp.push_back(val);
                }
                // однотипные читаемые значения есть (даже все в нулях) — структура жива
                if (k0 && shapeOk) *shapeOk = true;
                if (tmp.size() >= 1) {
                    objs = tmp;
                    printf("[esp] dict FOUND kc=0x%zx il=0x%x dict=0x%x e=%u,c=%u n=%u\n",
                           (size_t)klass, ilOff, dOff, (unsigned)ec[0], (unsigned)ec[1], (unsigned)tmp.size());
                    fflush(stdout);
                    return true;
                }
            }
        }
    }
    return false;
}

// yaw/pitch -> forward (градусы, yaw от +Z к +X).
void dirFromLook(float yaw, float pitch, V3f& d) {
    // MouseLook.Kkz и lookAngle в ГРАДУСАХ.
    const float kD2R = 0.01745329252f;
    float y = yaw * kD2R, p = pitch * kD2R;
    float cy = cosf(y), sy = sinf(y), cp = cosf(p), sp = sinf(p);
    d = {sy * cp, sp, cy * cp};
}

bool w2s(const V3f& cam, const V3f& fwd, const V3f& p,
         float tanHalf, float aspect, float w, float h, V2f& out) {
    V3f up{0, 1, 0};
    // right = up x fwd (для Unity: Y x Z = X). Было перепутано (fwd x up) —
    // экран зеркалился лево-право и аим уводило не туда.
    V3f right{up.y * fwd.z - up.z * fwd.y, up.z * fwd.x - up.x * fwd.z,
               up.x * fwd.y - up.y * fwd.x}; // up x fwd
    float rl = sqrtf(right.x * right.x + right.y * right.y + right.z * right.z);
    if (rl < 1e-6f) return false;
    right = {right.x / rl, right.y / rl, right.z / rl};
    V3f cup{fwd.y * right.z - fwd.z * right.y, fwd.z * right.x - fwd.x * right.z,
             fwd.x * right.y - fwd.y * right.x}; // fwd x right
    V3f v{p.x - cam.x, p.y - cam.y, p.z - cam.z};
    float z = v.x * fwd.x + v.y * fwd.y + v.z * fwd.z;
    if (z < 0.5f) return false;
    float x = v.x * right.x + v.y * right.y + v.z * right.z;
    float y = v.x * cup.x + v.y * cup.y + v.z * cup.z;
    out.x = w * 0.5f + (x / z) / (tanHalf * aspect) * (w * 0.5f);
    out.y = h * 0.5f - (y / z) / tanHalf * (h * 0.5f);
    return true;
}

} // namespace

// Shared by ESP rendering and aimbot so the visible head marker and the aim
// point cannot silently use different anchors.
inline V3f boneWorld(const Ent& e, int idx);

static double nowSec() {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec * 1e-9;
}

static void collectSnapshot(int screenW, int screenH) {
    V3f cam{0, 0, 0};
    V3f fwd{0, 0, 1};
    bool haveCam = false;
    float lookDbg[2] = {0, 0};
    std::vector<Ent> ents;
    const double nowTick = nowSec();
    // Детектор короткого обрыва (только local в кадре) — храним счётчик, а не копию всего мира.
    unsigned prevEnemies = 0;
    {
        std::lock_guard<std::mutex> lock(s_snapshotMu);
        for (const auto& e : s_world) if (!e.local) prevEnemies++;
    }
    // internal collector УДАЛЁН (модуль выпилен из Android.mk, либа чистится
    // инжектором): esp.bin мог писать только зомби из прошлого инжекта, а его
    // данные дёргали экран между internal/external. Только external-цепочка.
    // dict-путь в своём скоупе чтобы goto draw его перепрыгивал легально
    {
    static int resolverPid = -1;
    static double nextResolverTry = 0.0;
    static unsigned resolverFailures = 0;
    if (resolverPid != g_pid) {
        resolverPid = g_pid;
        // The game process can exist long before libil2cpp/metadata and the
        // PlayerManager static list are initialized.  A failed early scan must
        // not permanently disable the resolver for this PID.
        nextResolverTry = 0.0;
        resolverFailures = 0;
    }
    if (!g_pmKlass) {
        const double resolverNow = nowSec();
        if (resolverNow < nextResolverTry) return;
        double t0 = resolverNow;
        bool ok = resolveList();
        if (ok) {
            resolverFailures = 0;
            nextResolverTry = 0.0;
            printf("[esp] scan %.1fs -> FOUND\n", nowSec() - t0);
        } else {
            ++resolverFailures;
            // Back off expensive metadata scans while the game is still at
            // its loading/menu stage, but keep retrying for a late world load.
            // The cap keeps entering a server responsive without a cheat
            // restart even after a long stay in the main menu.
            const unsigned step = std::min(resolverFailures, 4u);
            const double retryDelay = std::min(5.0, 0.5 * (double)(1u << (step - 1u)));
            nextResolverTry = nowSec() + retryDelay;
            printf("[esp] scan %.1fs -> retry in %.1fs (attempt %u)\n",
                   nowSec() - t0, retryDelay, resolverFailures);
        }
        fflush(stdout);
        return;
    }

    std::vector<uint64_t> objs0;
    // Сначала точный List из дампа, потом fallback dict-цепочка. Хватает 1 живого (соло).
    // klass НЕ сбрасываем пока структура цела (shape) — в меню живых нет, это норма.
    static double emptySince = 0;
    static double lastWaitLog = 0;
    {
        bool shapeL = false, shapeD = false;
        bool haveLive = false;
        if (readPlayersViaList(g_pmKlass, objs0, &shapeL) && !objs0.empty()) haveLive = true;
        else if (readPlayersViaDict(g_pmKlass, objs0, &shapeD) && !objs0.empty()) haveLive = true;
        if (!haveLive) {
            double noww = nowSec();
            if (!shapeL && !shapeD) {
                printf("[esp] klass 0x%zx dead (shape lost), rescanning\n", (size_t)g_pmKlass);
                fflush(stdout);
                emptySince = 0;
                g_pmKlass = 0; g_listObj = 0; g_dropLocal = true;
                // Retry immediately if the old class was unloaded/replaced.
                resolverFailures = 0;
                nextResolverTry = 0.0;
                return;
            }
            if (emptySince == 0) emptySince = noww;
            if (noww - lastWaitLog > 5.0) {
                lastWaitLog = noww;
                printf("[esp] klass 0x%zx alive, waiting live players (%.0fs, зайди в мир/заспавнись)\n",
                       (size_t)g_pmKlass, noww - emptySince);
                fflush(stdout);
            }
            if (noww - emptySince > 120.0) { emptySince = 0; g_pmKlass = 0; g_listObj = 0; }
            return;
        }
        emptySince = 0;
    }
    uint32_t n = (uint32_t)objs0.size();

    // локальный игрок: поза + взгляд для камеры.
    // Приоритет: флаг isLocal -> валидный MouseLook -> первый с валидным взглядом (соло).
    haveCam = false;
    lookDbg[0] = lookDbg[1] = 0;
    ents.clear();
    int dbgFlagLocal = 0, dbgMl = 0;
    std::vector<uint64_t> objs;
    objs.reserve(n);
    for (uint32_t i = 0; i < n; i++) {
        uint64_t obj = objs0[i];
        Ent e;
        if (!readEnt(obj, e)) continue;
        uint64_t ml = 0;
        if (readU64(obj + kMouseLookOff, ml) && ml) dbgMl++;
        if (e.local) dbgFlagLocal++;
        ents.push_back(e); objs.push_back(obj);
    }
    // Детектор короткого обрыва: раньше был полный игроков список и вдруг 0 —
    // держим старый кадр до 1.5 сек пока не восстановится. Полную копию s_world
    // больше не делаем (дорого при 30+ игроках), используем счётчик.
    {
        static double dropSince = 0;
        unsigned newEnemies = 0;
        for (const auto& e : ents) if (!e.local) newEnemies++;
        if (prevEnemies > 0 && newEnemies == 0) {
            if (dropSince == 0) dropSince = nowTick;
            if (nowTick - dropSince < 1.5) return;
        } else {
            dropSince = 0;
        }
    }
    // Команда по указателям tl + флаг isTeammate (ИЛИ-логика):
    // тиммейт если ЛЮБОЙ сигнал так говорит. Старый код перезатирал флаг
    // сравнением указателей — при копиях tl-объекта своих кидало во враги.
    {
        uint64_t localTl = 0;
        for (auto& e : ents) {
            if (e.local && e.teamTl) { localTl = e.teamTl; break; }
        }
        if (localTl) {
            for (auto& e : ents) {
                if (e.teamTl && e.teamTl == localTl) e.enemy = false;
            }
        }
        // TEAM/DEAD-диагностика: сырые значения по всем (раз в 120 тиков)
        {
            static int tdN = 0;
            if (g_espDebug && (tdN++ % 120) == 0) {
                printf("[esp] TEAMDBG localTl=0x%zx n=%u\n", (size_t)localTl, (unsigned)ents.size());
                for (auto& e : ents) {
                    uint64_t zb = 0, tn = 0, cl = 0, iv = 0;
                    std::string tns, cls, ivs;
                    if (e.id && readU64(e.id + 0x120, zb) && zb && !(zb & 7)) {
                        if (readU64(e.id + 0x2A0, tn) && tn && !(tn & 7))
                            tns = readIl2cppString(tn); // teamName 0x2A0
                        if (readU64(e.id + 0x2B0, cl) && cl && !(cl & 7))
                            cls = readIl2cppString(cl); // clanId 0x2B0
                        if (readU64(zb + 0xB0, iv) && iv && !(iv & 7))
                            ivs = readIl2cppString(iv);
                    }
                    printf("[esp]   '%s' loc=%d en=%d tm=%d fr=%d resp=%d tl=0x%zx zb=0x%zx team='%s' clan='%s' inv='%s'\n",
                           e.name.c_str(), (int)e.local, (int)e.enemy,
                           (int)e.tmRaw, (int)e.isFriendRaw, (int)e.respRaw,
                           (size_t)e.teamTl, (size_t)zb,
                           tns.c_str(), cls.c_str(), ivs.c_str());
                    // словарь тиммейтов локального: layout Dict (buckets? entries? count?)
                    if (e.local && zb) {
                        uint64_t d = 0;
                        if (readU64(zb + 0x70, d) && d && !(d & 7)) {
                            uint64_t p10 = 0, p18 = 0, p20 = 0, p28 = 0;
                            uint32_t c10 = 0, c18 = 0, c20 = 0, c28 = 0;
                            readU64(d + 0x10, p10); readU64(d + 0x18, p18);
                            readU64(d + 0x20, p20); readU64(d + 0x28, p28);
                            readU32(d + 0x10, c10); readU32(d + 0x18, c18);
                            readU32(d + 0x20, c20); readU32(d + 0x28, c28);
                            printf("[esp]   MATES dict=0x%zx +10=(p=0x%zx c=%u) +18=(p=0x%zx c=%u) +20=(p=0x%zx c=%u) +28=(p=0x%zx c=%u)\n",
                                   (size_t)d, (size_t)p10, c10, (size_t)p18, c18,
                                   (size_t)p20, c20, (size_t)p28, c28);
                            // кандидаты entries-массивов: читаем первые ключи
                            for (int ci = 0; ci < 4; ci++) {
                                uint64_t arr = ci == 0 ? p10 : ci == 1 ? p18 : ci == 2 ? p20 : p28;
                                if (!arr || (arr & 7)) continue;
                                uint64_t alen = 0;
                                if (!readU64(arr + 24, alen) || alen == 0 || alen > 64) continue;
                                printf("[esp]   MATES arr@+0x%x len=%llu\n", 0x10 + ci * 8, (unsigned long long)alen);
                                for (uint64_t k = 0; k < alen && k < 4; k++) {
                                    uint8_t en[24] = {0};
                                    if (!rpatch::readMem(g_pid, arr + 32 + k * 24, en, sizeof(en))) continue;
                                    int32_t hc = 0; uint64_t key = 0;
                                    memcpy(&hc, en, 4); memcpy(&key, en + 8, 8);
                                    char ks[40] = {0};
                                    if (key && !(key & 7)) {
                                        std::string s = readIl2cppString(key);
                                        snprintf(ks, sizeof(ks), "%s", s.c_str());
                                    }
                                    printf("[esp]     [%llu] hc=%d key='%s'\n", (unsigned long long)k, hc, ks);
                                }
                            }
                        } else {
                            printf("[esp]   MATES noread\n");
                        }
                    }
                }
                fflush(stdout);
            }
        }
    }
    // проход 1: флаг isLocal + взгляд. ЛИПКИЙ local: держим выбранный объект
    // пока он в списке с валидным взглядом, иначе камера скачет между
    // двумя local-флагованными объектами и боксы прыгают.

    static int sLocalPid = -1;
    if (sLocalPid != g_pid) { g_sLocalObj = 0; sLocalPid = g_pid; } // новая сессия — старый id мог переиспользоваться
    if (g_dropLocal) { g_sLocalObj = 0; g_dropLocal = false; } // klass умер — перепроверить камеру по флагу
    // Позиция камеры = worldCameraRoot (Transform глаз) прямо из игры.
    // Синтетика "ноги + 1.6м" ломалась при смерти: камера падает к земле, а ESP
    // считал её на уровне глаз стоящего игрока и всё уезжало. Transform падает
    // вместе с камерой, поэтому ESP остаётся привязан к настоящему виду.
    // КАМЕРА = worldCameraRoot (+0x68) — настоящий Transform глаз.
    // Он падает вместе с камерой смерти, поэтому ESP остаётся привязан к виду.
    // ВАЖНО: e.eye для этого не годится — когда lastTickPosition жив, туда
    // кладутся НОГИ (eye==pos, dy==0), и проверка "глаза выше ног" отбрасывала
    // значение, тихо возвращая старое поведение ноги+1.6. Читаем Transform явно.
    auto camPosOf = [&](size_t i) -> V3f {
        V3f tp{0, 0, 0};
        if (readPlayerPosViaTransform(objs[i], tp) && saneVec(tp)) {
            float dy = tp.y - ents[i].pos.y;
            // окно: от лежащей камеры смерти (-1м) до роста стоя (+3м)
            if (dy > -1.5f && dy < 3.0f) return tp;
        }
        return V3f{ents[i].pos.x, ents[i].pos.y + kEyeH, ents[i].pos.z};
    };
    auto pickCam = [&](size_t i) {
        float yaw = 0, pitch = 0;
        // Труп/наблюдатель: взгляд читаем всё равно — камера смерти живая,
        // иначе ESP замирал на последнем "живом" кадре (виды расходились).
        if (!readLookYawPitch(objs[i], yaw, pitch) && !ents[i].dead) return false;
        lookDbg[0] = yaw; lookDbg[1] = pitch;
        cam = camPosOf(i);
        dirFromLook(yaw, pitch, fwd);
        for (size_t j = 0; j < ents.size(); j++) ents[j].local = (j == i);
        haveCam = true;
        g_sLocalObj = objs[i];
        return true;
    };
    // Камера строго по источнику, без прыжков на чужого:
    // 1) липкий + флаг (стабильность), 2) любой флаг (флаг бьёт историю),
    // 3) без флагов — едем на том же объекте (флаг мог бито прочитаться),
    // 4) бутстрап только без истории. Иначе — держим прошлый кадр, а не хватаем чужого.
    if (g_sLocalObj) {
        for (size_t i = 0; i < ents.size() && !haveCam; i++) {
            if (!ents[i].local || objs[i] != g_sLocalObj) continue;
            if (pickCam(i)) sDbgPass = 1;
        }
    }
    for (size_t i = 0; i < ents.size() && !haveCam; i++) {
        if (!ents[i].local) continue;
        if (pickCam(i)) sDbgPass = 1;
    }
    // проход 2: флагов нет вообще — держим тот же объект (не первого с ml!).
    // Старый код брал первого с валидным взглядом — им оказывался враг,
    // камера телепортировалась на ±50м и ЕСП «плавал».
    if (!haveCam && dbgFlagLocal == 0 && g_sLocalObj) {
        for (size_t i = 0; i < ents.size() && !haveCam; i++) {
            if (objs[i] != g_sLocalObj) continue;
            if (pickCam(i)) sDbgPass = 2;
        }
    }
    // проход 3: бутстрап — истории нет, флагов нет (старт/битые флаги).
    if (!haveCam && dbgFlagLocal == 0 && !g_sLocalObj) {
        for (size_t i = 0; i < ents.size() && !haveCam; i++) {
            uint64_t ml = 0;
            if (!(readU64(objs[i] + kMouseLookOff, ml) && ml)) continue;
            float yaw = 0, pitch = 0;
            if (!readLookYawPitch(objs[i], yaw, pitch)) continue;
            lookDbg[0] = yaw; lookDbg[1] = pitch;
            cam = camPosOf(i);
            dirFromLook(yaw, pitch, fwd);
            for (size_t j = 0; j < ents.size(); j++) ents[j].local = (j == i);
            haveCam = true;
            g_sLocalObj = objs[i];
            sDbgPass = 3;
        }
    }
    sDbgFlagLocal = dbgFlagLocal;
    // QUAT-скан m_LookRoot локального (редко, для выбора цепочки вращения камеры)
    {
        static int qn = 0;
        // TRACE редко: контроль маппинга pitch (Kzd.y сантирадианы)
        if (g_espDebug && (qn++ % 600) == 0) {
            for (size_t i = 0; i < ents.size(); i++) {
                if (!ents[i].local) continue;
                // все кандидаты взгляда: Kzd 0x4C, Kzx 0x70, Kkz 0x88, lookAngle 0x1C8
                uint64_t ml = 0;
                float a[2] = {0,0}, b[2] = {0,0}, c[2] = {0,0}, la = 0;
                bool oka = false, okb = false, okc = false, okl = false;
                if (readU64(objs[i] + kMouseLookOff, ml) && ml && !(ml & 7)) {
                    oka = rpatch::readMem(g_pid, ml + 0x4C, a, 8);
                    okb = rpatch::readMem(g_pid, ml + 0x70, b, 8);
                    okc = rpatch::readMem(g_pid, ml + 0x88, c, 8);
                }
                okl = readF32(objs[i] + kLookAngleOff, la);
                printf("[esp] TRACE Kzd=%.1f,%.1f Kkz=%.1f,%.1f lookAngle=%.1f cam=%.0f,%.0f,%.0f\n",
                       a[0], a[1], c[0], c[1], la, ents[i].pos.x, ents[i].pos.y, ents[i].pos.z);
                fflush(stdout);
                break;
            }
        }
    }
    // детектор скачков yaw: каждый тик сравниваем с прошлым
    {
        static float prevYaw = 0;
        static int prevSrc = 0;
        static bool havePrev = false;
        // источник текущего yaw перечитывать дорого — берём из lookDbg через src-маркер:
        // pass1/2 почти всегда MouseLook(1); уточняем дешёвым признаком pitch!=0
        int curSrc = (lookDbg[1] != 0.f) ? 1 : sDbgPass; // грубо, детали в YAWJUMP
        if (havePrev) {
            float d = fabsf(lookDbg[0] - prevYaw);
            if (d > 180.f) d = 360.f - d;
            if (d > 25.f && g_espDebug) {
                printf("[esp] YAWJUMP prev=%.1f -> cur=%.1f d=%.1f pass=%d n=%u\n",
                       prevYaw, lookDbg[0], d, sDbgPass, (unsigned)ents.size());
                fflush(stdout);
            }
        }
        prevYaw = lookDbg[0]; prevSrc = curSrc; havePrev = true;
    }
    static int noCamLog = 0;
    static int noCamStreak = 0;
    if (!haveCam) {
        if (g_espDebug && (noCamLog++ % 60) == 0) {
            printf("[esp] nocam n=%u flagLocal=%d mlValid=%d\n", (unsigned)ents.size(), dbgFlagLocal, dbgMl);
            for (size_t d = 0; d < ents.size() && d < 12; d++) {
                float yawDbg = 0, pitchDbg = 0;
                bool yok = readLookYawPitch(objs[d], yawDbg, pitchDbg);
                printf("[esp]  ent%u pos=%.0f,%.0f,%.0f local=%d yawOk=%d yaw=%.2f pitch=%.2f\n",
                       (unsigned)d, ents[d].pos.x, ents[d].pos.y, ents[d].pos.z,
                       (int)ents[d].local, (int)yok, yok ? yawDbg : 0.f, yok ? pitchDbg : 0.f);
            }
            fflush(stdout);
        }
        // без камеры кеш не валиден вечно — даём шанс пересканить klass
        if (++noCamStreak > 120) { noCamStreak = 0; g_pmKlass = 0; g_listObj = 0; g_dropLocal = true; }
        // показываем последний живой кадр чтобы экран не мигал
        return;
    }
    noCamStreak = 0;
    // clamp углов: одиночный мусор чтения (±100°/тик) не применяем вообще,
    // а реальный скачок (респаун) подтверждается повтором и применяется сразу.
    // Старый slew давал ~10 тиков плывущих боксов на каждый глитч.
    {
        static float cYaw = 0, cPitch = 0;
        static bool cHave = false;
        static int cPid = -1;
        static float pYaw = 0, pPitch = 0;
        static bool havePend = false;
        if (!cHave || cPid != g_pid) {
            cYaw = lookDbg[0]; cPitch = lookDbg[1];
            cHave = true; cPid = g_pid; havePend = false;
        } else {
            float dy = lookDbg[0] - cYaw;
            while (dy > 180.f) dy -= 360.f;
            while (dy < -180.f) dy += 360.f;
            float dp = lookDbg[1] - cPitch;
            if (dy <= 90.f && dy >= -90.f && dp <= 60.f && dp >= -60.f) {
                cYaw = lookDbg[0]; cPitch = lookDbg[1]; havePend = false;
            } else if (havePend &&
                       fabsf(wrap180(lookDbg[0] - pYaw)) < 140.f &&
                       fabsf(lookDbg[1] - pPitch) < 90.f) {
                cYaw = lookDbg[0]; cPitch = lookDbg[1]; havePend = false; // подтверждено — снап
            } else {
                pYaw = lookDbg[0]; pPitch = lookDbg[1]; havePend = true; // ждём подтверждение, держим старые
            }
        }
        lookDbg[0] = cYaw; lookDbg[1] = cPitch;
        dirFromLook(cYaw, cPitch, fwd);
    }
    } // конец dict-скоупа

    float aspect = (float)screenW / (float)screenH;
    // FOV: живой из игры (s_gameFov, тиком раньше) -> fallback ручная настройка.
    float camFov = (s_gameFov > 30.f && s_gameFov < 100.f) ? s_gameFov : Vars.Settings.camFov;
    if (!(camFov > 30.f && camFov < 130.f)) camFov = 53.f;
    float tanHalf = tanf(camFov * 0.5f * 0.0174533f);
    // Scope-state + живой FOV: FPManager локального (player+0x90), один рид +0x9C..+0xAC:
    // KtA +0x9C (FOV: 70 хип / 50.9 scope), KtK +0xA8 (0 хип, 1 scope).
    // Фикс 53° давал радиальную ошибку ×1.4 от бедра — боксы «уплывали» от моделей.
    {
        s_scoped = false;
        for (auto& e : ents) {
            if (!e.local || !e.id) continue;
            uint64_t fp = 0;
            if (!readU64(e.id + 0x90, fp) || !fp || (fp & 7)) break;
            uint8_t raw[16] = {0};
            if (!rpatch::readMem(g_pid, fp + 0x9C, raw, sizeof(raw))) break;
            float fov, ktk;
            memcpy(&fov, raw, 4); memcpy(&ktk, raw + 12, 4);
            if (std::isfinite(ktk)) s_scoped = ktk > 0.5f;
            if (std::isfinite(fov) && fov > 30.f && fov < 100.f) s_gameFov = fov;
            // Кастомный FOV мира: держим оба поля (текущий + цель лерпа).
            // ESP-проекция читает то же поле — боксы остаются на моделях автоматически.
            if (Vars.Player.camerafov) {
                float cf = Vars.Player.customFov;
                if (cf > 30.f && cf < 130.f) {
                    rpatch::writeF32(g_pid, fp + 0x9C, cf);
                    rpatch::writeF32(g_pid, fp + 0xA0, cf);
                    s_gameFov = cf;
                }
            }
            // Кастомный FOV рук: FPObject (+0x50) normalFOV +0x80 / aimFOV +0x84.
            // Игра пересоздаёт view-объект (фрейм дефолта = мигание), поэтому
            // держим последние 4 объекта сразу (klass проверяем — не пишем в чужое).
            if (Vars.Player.armfov) {
                float av = Vars.Player.armfovValue;
                if (av >= 20.f && av <= 120.f) {
                    uint64_t cur = 0;
                    if (readU64(fp + 0x50, cur) && cur && !(cur & 7)) {
                        static uint64_t recent[4] = {0, 0, 0, 0};
                        if (cur != recent[0]) {
                            recent[3] = recent[2]; recent[2] = recent[1];
                            recent[1] = recent[0]; recent[0] = cur;
                        }
                        int32_t want = (int32_t)av;
                        int32_t v[2] = {want, want};
                        for (int ri = 0; ri < 4; ri++) {
                            uint64_t o = recent[ri];
                            if (!o || (o & 7)) continue;
                            uint64_t ok = 0, onp = 0;
                            char onm[48] = {0};
                            if (!readU64(o, ok) || !ok ||
                                !readU64(ok + 16, onp) || !onp ||
                                !readCString(onp, onm, sizeof(onm)) ||
                                strncmp(onm, "FP", 2) != 0) { // только FP*: FPHitscan/FPTool/...
                                if (ri > 0) recent[ri] = 0;
                                continue;
                            }
                            // RACE-замер только для свежего объекта
                            if (ri == 0) {
                                static int armSamp = 0, armMiss = 0, armMissSc = 0;
                                int32_t rb[2] = {0, 0};
                                uint8_t tmp[8] = {0};
                                if (rpatch::readMem(g_pid, o + 0x80, tmp, sizeof(tmp))) {
                                    memcpy(rb, tmp, sizeof(tmp));
                                    if (rb[0] != want || rb[1] != want) {
                                        armMiss++;
                                        if (s_scoped.load()) armMissSc++;
                                    }
                                }
                                if ((++armSamp % 300) == 0) {
                                    printf("[esp] ARMRACE miss=%d/300 scMiss=%d scoped=%d\n",
                                           armMiss, armMissSc, (int)s_scoped.load());
                                    fflush(stdout);
                                    armMiss = 0; armMissSc = 0;
                                }
                            }
                            rpatch::writeMem(g_pid, o + 0x80, v, sizeof(v));
                        }
                    }
                }
            }
            break;
        }
    }
    // NO-RECOIL + NO-SPREAD: цепочка уже проверена вживую.
    // RecoilConfig +0x68 (Min/Max +0x18), DispersionConfig +0x60 (5 флоатов +0x18).
    {
        static int rcN = 0;
        if ((Vars.Player.norecoil || Vars.Player.nospread) && (rcN++ % 2) == 0) {
            for (auto& e : ents) {
                if (!e.local || !e.id) continue;
                uint64_t fp = 0, cur = 0, cfg = 0;
                if (!readU64(e.id + 0x90, fp) || !fp || (fp & 7)) break;
                if (!readU64(fp + 0x50, cur) || !cur || (cur & 7)) break;
                if (!readU64(cur + 0x128, cfg) || !cfg || (cfg & 7)) break;
                if (Vars.Player.norecoil) {
                    uint64_t rec = 0;
                    if (readU64(cfg + 0x68, rec) && rec && !(rec & 7)) {
                        float z[4] = {0, 0, 0, 0};
                        rpatch::writeMem(g_pid, rec + 0x18, z, sizeof(z)); // Min+Max = 0
                    }
                }
                if (Vars.Player.nospread) {
                    uint64_t disp = 0;
                    if (readU64(cfg + 0x60, disp) && disp && !(disp & 7)) {
                        float z[5] = {0, 0, 0, 0, 0};
                        rpatch::writeMem(g_pid, disp + 0x18, z, sizeof(z)); // min/max/fire/increase = 0
                    }
                }
                if ((rcN % 600) == 0) {
                    float mn[2] = {0, 0}, mx[2] = {0, 0}, dp = -1.f;
                    uint64_t rec = 0, disp = 0;
                    uint8_t rb[20] = {0};
                    if (readU64(cfg + 0x68, rec) && rec && !(rec & 7) &&
                        rpatch::readMem(g_pid, rec + 0x18, rb, 16)) {
                        memcpy(mn, rb, 8); memcpy(mx, rb + 8, 8);
                    }
                    if (readU64(cfg + 0x60, disp) && disp && !(disp & 7))
                        readF32(disp + 0x18, dp);
                    printf("[esp] NOSHOOT min=%.2f,%.2f max=%.2f,%.2f dispMin=%.2f\n",
                           mn[0], mn[1], mx[0], mx[1], dp);
                    fflush(stdout);
                }
                break;
            }
        }
    }
    // Быстрый висибл-чек на воркере: только «вперёд-в-конусе + разумная дистанция».
    // Точный w2s всё равно делается в рендере, а дублировать его здесь на 25 Гц
    // по 30 игрокам = 90 w2s + 30 boneWorld просто так, отсюда и 10 FPS.
    // В r-квадратах избегаем sqrt до самого момента необходимости.
    for (auto& e : ents) {
        e.visible = false;
        if (e.local || e.dead) continue;
        float dxv = e.pos.x - cam.x, dyv = e.pos.y - cam.y, dzv = e.pos.z - cam.z;
        float dv2 = dxv*dxv + dyv*dyv + dzv*dzv;
        if (dv2 < 0.09f || dv2 > 800.f*800.f) continue;
        // впереди камеры и в угле ~ FOV (cos ≈ 0.25 ~ 75° в каждую сторону)
        float fwdDot = dxv*fwd.x + dyv*fwd.y + dzv*fwd.z;
        if (fwdDot > 0.35f) {
            e.visible = true;
        } else if (fwdDot > 0.f && dv2 < 16.f) {
            e.visible = true;
        }
    }
    {
        std::lock_guard<std::mutex> lock(s_snapshotMu);
        s_world.swap(ents);            // swap без копирования
        s_cam = cam;
        s_fwd = fwd;
        s_tanHalf = tanHalf;
        s_aspect = aspect;
        s_sw = screenW;
        s_sh = screenH;
        s_haveCam = true;
        s_lookDbg[0] = lookDbg[0];
        s_lookDbg[1] = lookDbg[1];
        s_lastCollect = nowSec();
        ++s_snapshotSeq;
    }
}

static unsigned countPlayers(const std::vector<Ent>& world) {
    unsigned count = 0;
    // clientPlayerList contains PlayerManager entries, not NPCBase bots.
    // Count every live non-local player regardless of camera visibility.
    for (const auto& e : world)
        if (!e.local && !e.dead) count++;
    return count;
}

// Компактный верхне-центральный индикатор игроков — тоньше, выше, с иконкой-точкой.
static void drawPlayerCount(int screenW, unsigned count) {
    char buf[32];
    snprintf(buf, sizeof(buf), "%u", count);
    const char* label = "players";
    ImFont* f = ImGui::GetFont();
    const float fs = 16.0f;
    ImVec2 ls = f->CalcTextSizeA(fs, FLT_MAX, 0, label);
    ImVec2 ns = f->CalcTextSizeA(fs + 2.f, FLT_MAX, 0, buf);
    const float padX = 12.f, padY = 7.f;
    const float gap = 8.f;
    const float iconR = 4.f;
    const float contentW = iconR*2 + 4.f + ls.x + gap + ns.x;
    const float contentH = ls.y;
    const float cx = (float)screenW * 0.5f;
    const float w = contentW + padX * 2.f;
    const float h = contentH + padY * 2.f;
    const float left = cx - w * 0.5f;
    const float top = 14.f;
    ImDrawList* dl = ImGui::GetBackgroundDrawList();
    dl->AddRectFilled(ImVec2(left, top), ImVec2(left + w, top + h),
                      IM_COL32(14, 16, 22, 210), 8.f);
    dl->AddRect(ImVec2(left, top), ImVec2(left + w, top + h),
                IM_COL32(88, 166, 255, 180), 8.f, 0, 1.0f);
    // Зелёная точка-индикатор сети
    const float dy = top + h * 0.5f;
    dl->AddCircleFilled(ImVec2(left + padX + iconR, dy), iconR,
                        IM_COL32(90, 220, 110, 230), 16);
    // label
    float tx = left + padX + iconR*2 + 4.f;
    dl->AddText(f, fs, ImVec2(tx, top + padY),
                IM_COL32(170, 190, 220, 255), label);
    tx += ls.x + gap;
    // счётчик жирный
    dl->AddText(f, fs + 2.f, ImVec2(tx, top + padY - 1.f),
                IM_COL32(245, 248, 255, 255), buf);
}

// Простейший счётчик кадров: инкрементится в collectSnapshot при каждом новом
// снимке. aimAngles использует его чтобы не лидить по одному и тому же кадру.
static uint64_t s_snapshotSeq = 0;

void tick(int screenW, int screenH) {
    static int observedPid = -1;
    static double nextPidCheck = 0;
    const double frameNow = nowSec();
    if (observedPid <= 0 || frameNow >= nextPidCheck) {
        observedPid = rpatch::pidOf("com.catsbit.oxidesurvivalisland");
        nextPidCheck = frameNow + (observedPid > 0 ? 1.0 : 0.2);
    }
    int pid = observedPid;
    if (pid <= 0) {
        // PID не найден — не очищаем старый кэш, чтобы экран не мигал
        // при кратких пропаданиях процесса.
        if (!s_renderHave) return;
    } else {
        if (pid != g_pid) {
            g_pid = pid;
            g_pmKlass = 0;
            g_listObj = 0;
            g_needLookReset = true;
            {
                std::lock_guard<std::mutex> lock(s_snapshotMu);
                s_world.clear(); s_haveCam = false;
            }
            s_renderHave = false;
            s_renderWorld.clear();
#if defined(__aarch64__)
            printf("[esp] target pid=%d abi=arm64-v8a\n", g_pid);
#else
            printf("[esp] target pid=%d abi=non-arm64\n", g_pid);
#endif
            fflush(stdout);
        }
        // СИНХРОННЫЙ сбор кадра — не чаще 30 раз/сек (33 мс).
        // Всё чтение памяти происходит ЗДЕСЬ, один раз подряд, без потоков.
        if (frameNow - s_lastCollect >= 0.033) {
            collectSnapshot(screenW, screenH);
        }
    }

    // Копируем актуальный снимок в рендер-кэш под коротким мьютексом.
    // Если нового нет — используем предыдущий кэш (это и даёт 60 FPS).
    {
        std::lock_guard<std::mutex> lock(s_snapshotMu);
        if (s_haveCam && !s_world.empty()) {
            s_renderWorld = s_world;
            s_renderCam   = s_cam;
            s_renderFwd   = s_fwd;
            s_renderTan   = s_tanHalf;
            s_renderAspect= s_aspect;
            s_renderW     = s_sw;
            s_renderH     = s_sh;
            s_renderHave  = true;
        }
    }
    if (!s_renderHave || s_renderWorld.empty()) return;

    // Далее идёт ровно тот же рендер-код что и раньше, но на локальных
    // копиях без каких-либо блокировок.
    const V3f cam = s_renderCam;
    const V3f fwd = s_renderFwd;
    const std::vector<Ent>& ents = s_renderWorld;
    float lookDbg[2] = { s_lookDbg[0], s_lookDbg[1] };

    // Базис камеры для стрелок.
    V3f worldUp{0.f, 1.f, 0.f};
    float fwu = fwd.x*worldUp.x + fwd.y*worldUp.y + fwd.z*worldUp.z;
    if (fwu > 0.99f)  worldUp = V3f{0.f, 0.f, -1.f};
    else if (fwu < -0.99f) worldUp = V3f{0.f, 0.f, 1.f};
    V3f camRight = {
        fwd.y*worldUp.z - fwd.z*worldUp.y,
        fwd.z*worldUp.x - fwd.x*worldUp.z,
        fwd.x*worldUp.y - fwd.y*worldUp.x
    };
    float crl = sqrtf(camRight.x*camRight.x + camRight.y*camRight.y + camRight.z*camRight.z);
    if (crl > 1e-5f) { camRight.x/=crl; camRight.y/=crl; camRight.z/=crl; }
    V3f camUp = {
        camRight.y*fwd.z - camRight.z*fwd.y,
        camRight.z*fwd.x - camRight.x*fwd.z,
        camRight.x*fwd.y - camRight.y*fwd.x
    };

    int screenW = s_renderW;
    int screenH = s_renderH;
    if (screenW <= 0) screenW = s_renderW;
    if (screenH <= 0) screenH = s_renderH;

    float aspect = s_renderAspect;
    if (aspect < 0.1f || aspect > 10.f) aspect = (float)screenW/(float)screenH;
    float tanHalf = s_renderTan;
    if (!(tanHalf > 0.15f && tanHalf < 5.f)) {
        float cf = Vars.Settings.camFov;
        if (!(cf > 30.f && cf < 130.f)) cf = 53.f;
        tanHalf = tanf(cf * 0.5f * 0.0174533f);
    }
    {
        float t = s_gameFov;
        if (t > 30.f && t < 100.f) tanHalf = tanf(t * 0.5f * 0.0174533f);
    }

    // Дебаг только по флагу, никогда в проде.
    if (g_espDebug) {
        static int dbgPos = 0;
        static double lastT = 0;
        if ((dbgPos++ % 120) == 0) {
            double now = nowSec();
            double fps = lastT > 0 ? 120.0/(now-lastT) : 0;
            lastT = now;
            printf("[esp] cam=%.1f,%.1f,%.1f fwd=%.2f,%.2f,%.2f yaw=%.2f pitch=%.2f n=%u fps=%.0f\n",
                   cam.x, cam.y, cam.z, fwd.x, fwd.y, fwd.z,
                   lookDbg[0], lookDbg[1], (unsigned)ents.size(), fps);
            fflush(stdout);
        }
    }

    if (Vars.ESP.count) drawPlayerCount(screenW, countPlayers(ents));
    if (!Vars.ESP.enable) return;

    ImDrawList* dl = ImGui::GetBackgroundDrawList();
    const float swF = (float)screenW, shF = (float)screenH;

    // Старение стрелок
    if (!s_arrows.empty()) {
        for (auto& kv : s_arrows) kv.second.miss++;
    }

    ImVec2 tracerOrg(swF*0.5f, shF);
    {
        int sel = Vars.ESP.EspCustom.lineselect;
        if (sel == 0) tracerOrg = ImVec2(swF*0.5f, 0.0f);
        else if (sel == 1) tracerOrg = ImVec2(swF*0.5f, shF*0.5f);
    }

    auto drawArrow = [&](const Ent& e) {
        V3f vv{e.pos.x - cam.x, e.pos.y - cam.y, e.pos.z - cam.z};
        float zz = vv.x*fwd.x + vv.y*fwd.y + vv.z*fwd.z;
        float xx = vv.x*camRight.x + vv.y*camRight.y + vv.z*camRight.z;
        float yy = vv.x*camUp.x + vv.y*camUp.y + vv.z*camUp.z;
        float dx = xx, dy = -yy;
        if (zz < 0.5f) { dx = -dx; dy = -dy; }
        if (dx*dx + dy*dy < 0.25f) return;
        ArrowSt& st = s_arrows[e.id];
        if (st.miss > 5) { st.dx = dx; st.dy = dy; }
        else { st.dx += (dx - st.dx)*0.5f; st.dy += (dy - st.dy)*0.5f; }
        st.miss = 0;
        float l = sqrtf(st.dx*st.dx + st.dy*st.dy);
        if (l < 1e-3f) return;
        float R = Vars.ESP.arrowRadius;
        if (!(R >= 40.f && R <= 500.f)) R = 140.f;
        float ex = swF*0.5f + st.dx/l*R;
        float ey = shF*0.5f + st.dy/l*R;
        float ux = st.dx/l, uy = st.dy/l;
        ImVec2 tip(ex + ux*10.f, ey + uy*10.f);
        ImVec2 b1(ex - uy*7.f - ux*6.f, ey + ux*7.f - uy*6.f);
        ImVec2 b2(ex + uy*7.f - ux*6.f, ey - ux*7.f - uy*6.f);
        ImVec4 arrCol = e.enemy ? espboxcolor : ImVec4(0.25f,0.95f,0.45f,1.0f);
        if (!e.visible) arrCol = ImVec4(arrCol.x*0.5f, arrCol.y*0.5f, arrCol.z*0.5f, 0.8f);
        dl->AddTriangleFilled(tip, b1, b2, ImColor(arrCol));
    };

    static const ImU32 kHi = IM_COL32(255,255,255,150);
    auto drawCornerFrame = [&](float left, float top, float right, float bottom,
                               ImU32 color, ImU32 fillCol) {
        const float span = std::min(right-left, bottom-top);
        const float arm  = std::max(8.f, std::min(14.f, span*0.22f));
        const float w    = 1.4f;
        if (Vars.ESP.xrayFill)
            dl->AddRectFilled(ImVec2(left,top), ImVec2(right,bottom), fillCol,
                              Vars.Settings.espround);
        dl->AddLine(ImVec2(left,top),         ImVec2(left+arm,top),    color,w);
        dl->AddLine(ImVec2(left,top),         ImVec2(left,top+arm),    color,w);
        dl->AddLine(ImVec2(right-arm,top),    ImVec2(right,top),       color,w);
        dl->AddLine(ImVec2(right,top),        ImVec2(right,top+arm),   color,w);
        dl->AddLine(ImVec2(left,bottom-arm),  ImVec2(left,bottom),     color,w);
        dl->AddLine(ImVec2(left,bottom),      ImVec2(left+arm,bottom), color,w);
        dl->AddLine(ImVec2(right-arm,bottom), ImVec2(right,bottom),    color,w);
        dl->AddLine(ImVec2(right,bottom-arm), ImVec2(right,bottom),    color,w);
        if (span > 30.f) {
            const float r = 1.3f;
            dl->AddCircleFilled(ImVec2(left,top),r,kHi,8);
            dl->AddCircleFilled(ImVec2(right,top),r,kHi,8);
            dl->AddCircleFilled(ImVec2(left,bottom),r,kHi,8);
            dl->AddCircleFilled(ImVec2(right,bottom),r,kHi,8);
        }
    };

    ImFont* nfCache = ImGui::GetFont();
    const float nFsCache = 12.5f;
    int shown = 0;

    for (auto& e : ents) {
        if (e.local) continue;
        if (e.dead) continue;

        V3f headBone = boneWorld(e, 0);
        float dx = e.pos.x - cam.x, dy = e.pos.y - cam.y, dz = e.pos.z - cam.z;
        float distSq = dx*dx + dy*dy + dz*dz;
        if (distSq < 0.09f || distSq > 800.f*800.f) continue;
        if (e.pos.x*e.pos.x + e.pos.z*e.pos.z < 25.f) continue;

        float boxTopY = e.pos.y + 1.8f;
        if (headBone.y + 0.2f < boxTopY) boxTopY = headBone.y + 0.2f;
        V3f top{e.pos.x, boxTopY, e.pos.z};

        // Грубый отсев "игрок за спиной" до w2s.
        float fwdDot = dx*fwd.x + dy*fwd.y + dz*fwd.z;
        if (fwdDot < 0.5f) {
            if (Vars.ESP.outOfScreen) drawArrow(e);
            continue;
        }

        V2f fb{0,0}, tb{0,0};
        bool vf = w2s(cam, fwd, e.pos, tanHalf, aspect, swF, shF, fb);
        bool vt = w2s(cam, fwd, top,  tanHalf, aspect, swF, shF, tb);
        float h = 0.f;
        const float dist = sqrtf(distSq);
        if (vf && vt) {
            h = fb.y - tb.y;
        } else if (vf && dist < 4.f) {
            float tH = tanHalf; if (!(tH > 0.1f && tH < 3.f)) tH = 0.5f;
            float d = dist < 0.8f ? 0.8f : dist;
            h = shF * 1.8f / (2.f * d * tH);
            tb = V2f{fb.x, fb.y - h};
            vt = true;
        } else {
            if (Vars.ESP.outOfScreen) drawArrow(e);
            continue;
        }
        if (h < 4.f) {
            if (Vars.ESP.outOfScreen) drawArrow(e);
            continue;
        }
        if (h > shF*1.5f) {
            float tH = tanHalf; if (!(tH > 0.1f && tH < 3.f)) tH = 0.5f;
            float d = dist < 0.8f ? 0.8f : dist;
            h = shF * 1.8f / (2.f * d * tH);
            if (h > shF*3.f) h = shF*3.f;
            if (h < shF*0.5f) h = shF*0.5f;
            tb.y = fb.y - h;
        }
        float bw = h * 0.5f;
        float x0 = tb.x - bw*0.5f, x1 = tb.x + bw*0.5f;
        const float M = 100.f;
        if (x1 < -M || x0 > swF+M || fb.y < -M || tb.y > shF+M) {
            if (Vars.ESP.outOfScreen) drawArrow(e);
            continue;
        }
        float cx = (x0+x1)*0.5f, cy = (fb.y+tb.y)*0.5f;
        if (cx < -1000.f || cx > swF+1000.f || cy < -1000.f || cy > shF+1000.f) {
            if (Vars.ESP.outOfScreen) drawArrow(e);
            continue;
        }

        ImVec4 b4 = e.enemy ? espboxcolor : ImVec4(0.25f,0.95f,0.45f,1.0f);
        ImVec4 n4 = e.enemy ? namecolor  : ImVec4(0.65f,1.0f,0.72f,1.0f);
        if (!e.visible) {
            float sc = 0.55f;
            b4 = ImVec4(b4.x*sc, b4.y*sc, b4.z*sc, 0.82f);
            n4 = ImVec4(n4.x*(sc+0.15f), n4.y*(sc+0.15f), n4.z*(sc+0.15f), 0.92f);
        }
        float drawX0 = x0 < 0.f ? 0.f : x0;
        float drawX1 = x1 > swF ? swF : x1;
        float drawTop = tb.y < 0.f ? 0.f : tb.y;
        float drawBot = fb.y > shF ? shF : fb.y;
        if (drawX1 - drawX0 < 2.f) { drawX0 = tb.x - 20.f; drawX1 = tb.x + 20.f; }

        if (Vars.ESP.box) {
            ImU32 frameColor = ImColor(b4);
            ImU32 fillCol    = ImColor(b4.x, b4.y, b4.z, 0.08f);
            drawCornerFrame(drawX0, drawTop, drawX1, drawBot, frameColor, fillCol);
            V2f hs{0,0};
            if (w2s(cam, fwd, headBone, tanHalf, aspect, swF, shF, hs)) {
                if (hs.x >= drawX0-4.f && hs.x <= drawX1+4.f &&
                    hs.y >= drawTop-4.f && hs.y <= drawBot+4.f) {
                    dl->AddCircleFilled(ImVec2(hs.x,hs.y), 2.6f, frameColor, 8);
                    dl->AddCircleFilled(ImVec2(hs.x,hs.y), 1.2f, IM_COL32(255,255,255,220), 8);
                }
            }
            dl->AddCircleFilled(ImVec2((drawX0+drawX1)*0.5f, drawBot-1.f),
                                1.8f, frameColor, 8);
        }
        if (Vars.ESP.tracer) {
            float endX = cx;
            float endY = fb.y;
            if (endY > shF) endY = shF;
            if (endY < 0.f) endY = 0.f;
            dl->AddLine(tracerOrg, ImVec2(endX, endY), ImColor(linecolor), 1.3f);
        }

        const bool hasName = Vars.ESP.name && !e.name.empty();
        const bool hasDistance = Vars.ESP.distance;
        if (hasName || hasDistance) {
            char distText[8] = {0};
            if (hasDistance) snprintf(distText, sizeof(distText), "%dm", (int)dist);
            ImVec2 nameSize = hasName ? nfCache->CalcTextSizeA(nFsCache, FLT_MAX, 0, e.name.c_str()) : ImVec2(0,0);
            ImVec2 distSize = hasDistance ? nfCache->CalcTextSizeA(nFsCache, FLT_MAX, 0, distText) : ImVec2(0,0);
            const float chipPadX = 6.f, chipPadY = 2.f;
            const float chipW = hasDistance ? distSize.x + chipPadX*2 : 0.f;
            const float gap = 6.f;
            const float textW = nameSize.x + (hasName&&hasDistance ? gap : 0.f) + chipW;
            if (textW > 0.5f) {
                const float padX = 8.f, padY = 4.f;
                float labelY = tb.y - nameSize.y - 10.f;
                if (labelY < 2.f) labelY = 2.f;
                if (labelY > shF - nameSize.y - 6.f) labelY = shF - nameSize.y - 6.f;
                float lblCx = (drawX0+drawX1)*0.5f;
                float labelLeft  = lblCx - textW*0.5f - padX;
                float labelRight = lblCx + textW*0.5f + padX;
                if (labelLeft < 2.f) { labelRight += 2.f-labelLeft; labelLeft = 2.f; }
                if (labelRight > swF-2.f) {
                    labelLeft -= labelRight - (swF-2.f);
                    labelRight = swF-2.f;
                }
                float labelBottom = labelY + nameSize.y + padY*2;
                dl->AddRectFilled(ImVec2(labelLeft,labelY-2.f),
                                  ImVec2(labelRight,labelBottom),
                                  IM_COL32(12,14,20,220), 5.f);
                ImU32 accent = ImColor(b4);
                dl->AddRectFilled(ImVec2(labelLeft,labelY+3.f),
                                  ImVec2(labelLeft+2.2f,labelBottom-3.f),
                                  accent, 1.f);
                if (lblCx >= labelLeft-2.f && lblCx <= labelRight+2.f) {
                    dl->AddLine(ImVec2(lblCx,labelBottom),
                                ImVec2(lblCx,tb.y-1.f),
                                ImColor(b4.x,b4.y,b4.z,0.5f), 1.0f);
                }
                float tx = labelLeft + padX;
                float ty = labelY + padY;
                if (hasName) {
                    dl->AddText(nfCache, nFsCache, ImVec2(tx,ty), ImColor(n4), e.name.c_str());
                    tx += nameSize.x + gap;
                }
                if (hasDistance) {
                    float chipL = tx;
                    float chipR = tx + chipW;
                    float chipT = ty - 1.f;
                    float chipB = ty + distSize.y + 2.f*chipPadY - 1.f;
                    dl->AddRectFilled(ImVec2(chipL,chipT), ImVec2(chipR,chipB),
                                      ImColor(b4.x,b4.y,b4.z,0.22f), 3.f);
                    dl->AddText(nfCache, nFsCache,
                                ImVec2(chipL+chipPadX, ty+chipPadY-1.f),
                                ImColor(distancecolor), distText);
                }
            }
        }
        shown++;
    }

    static int cleanCtr = 0;
    if ((++cleanCtr & 15) == 0 && !s_arrows.empty()) {
        for (auto it = s_arrows.begin(); it != s_arrows.end();) {
            if (it->second.miss > 15) it = s_arrows.erase(it);
            else ++it;
        }
    }
}


void lookDbgGet(float& yaw, float& pitch) {
    std::lock_guard<std::mutex> snapshotLock(s_snapshotMu);
    yaw = s_lookDbg[0];
    pitch = s_lookDbg[1];
}

bool isScoped() {
    return s_scoped.load();
}

void setDebug(bool on) {
    g_espDebug = on;
    printf("[esp] debug logs %s\n", on ? "ON" : "OFF");
    fflush(stdout);
}
// Selectable aim bones: Head/Neck/Chest/Pelvis/Arm/Leg.
// The head comes from the live worldCameraRoot Transform.  The remaining
// points are built between the live feet/root and head anchors; arms and legs
// also use the target's MouseLook yaw so they are not all collinear.  This is
// deliberately kept in world space and shared by target selection and angle
// calculation, so the menu choice changes the actual aim point.
static const float kBoneT[6] = {1.00f, 0.91f, 0.73f, 0.52f, 0.73f, 0.27f};
inline V3f boneWorld(const Ent& e, int idx) {
    if (idx < 0 || idx > 5) idx = 2;

    V3f feet = e.pos;
    V3f head = feet;
    // worldCameraRoot is the live head/eye bone and follows crouching.  Do
    // not use the nickname character anchor as the primary head: on this
    // build it follows the ESP box root and produces center-box aiming.
    if (e.haveHeadBone) head = e.headBone;
    else if (e.havePivot) head = e.pivot;
    else if (e.haveHead) head = e.head;
    else head.y += 1.60f;

    // Guard against a stale/misidentified head anchor making all other bones
    // fly far away from the player.  The regular player height is roughly
    // 1.2..2.8 m; clamp only the fallback geometry, never the true head.
    float height = head.y - feet.y;
    if (!std::isfinite(height) || height < 1.0f || height > 3.5f) {
        head = feet;
        head.y += 1.60f;
        height = 1.60f;
    }

    const float t = kBoneT[idx];
    V3f point{feet.x,
              feet.y + height * t,
              feet.z};
    if (idx == 0 && e.haveHeadBone) return e.headBone;
    if (idx == 0 && e.havePivot) return e.pivot;
    if (idx == 0 && e.haveHead) return e.head;

    // +yaw is atan2(x,z) in the same convention used by aimAngles.  Right is
    // (cos(yaw), 0, -sin(yaw)); use a small lateral offset for the selected
    // arm/leg while keeping their height tied to the measured body span.
    if (idx == 4 || idx == 5) {
        float yaw = e.haveBodyYaw ? e.bodyYaw * 0.01745329252f : 0.0f;
        float rightX = cosf(yaw);
        float rightZ = -sinf(yaw);
        const float lateral = idx == 4 ? 0.34f : 0.14f;
        point.x += rightX * lateral;
        point.z += rightZ * lateral;
    }
    return point;
}
// yaw игры = Kzd.y (накопленные градусы), pitch игры = lookAngle (+ = вниз).
// Замерено свайпами: +300px вправо = +72° yaw (0.24°/px),
// -200px вверх = +29° вверх (0.145°/px). Без записей в память.
// Угловой аим (без проекции в контуре управления):
// yaw игры = Kzd.y (накопленные градусы), pitch игры = lookAngle (+ = вниз).
// Замерено свайпами: +300px вправо = +72° yaw (0.24°/px),
// -200px вверх = +29° вверх (0.145°/px). Без записей в память.
// boneIdx: 0=Head(live worldCameraRoot), остальные — точки тела от root/head.

// Угловой аим для прямой записи: выборка цели из актуального snapshot
// (липкий лок по id + ближайший к центру в fovPx), но возвращаем
// желаемые углы и адреса записи вместо пикселей драга.
// yaw: ml+0x50 сырой (накопленный), pitch: player+0x1C8 lookAngle (+ = вниз),
// зеркало pitch: ml+0x4C (Kzd.x == lookAngle).
bool aimAngles(int boneIdx, float fovPx, AimWrite& out, bool visibleOnly) {
    std::lock_guard<std::mutex> snapshotLock(s_snapshotMu);
    out = AimWrite{};
    if (!s_haveCam || s_world.empty()) return false;
    float cx = (float)s_sw * 0.5f, cy = (float)s_sh * 0.5f;
    auto onScreen = [&](float x, float y) {
        return x >= -50.f && x <= (float)s_sw + 50.f && y >= -50.f && y <= (float)s_sh + 50.f;
    };
    static uint64_t lockId = 0;
    static int lockLost = 0;
    const Ent* tgt = nullptr;
    // UI-фильтр аима: dead/sleeping/team/distance/visible (меню Aim->FILTER/TARGET).
    auto aimOk = [&](const Ent& e) -> bool {
        if (e.local) return false;
        if (e.dead) return false;
        if (e.sleeping) return false;
        if (!e.enemy) return false;
        float dx0 = e.pos.x - s_cam.x, dy0 = e.pos.y - s_cam.y, dz0 = e.pos.z - s_cam.z;
        float d2 = dx0 * dx0 + dy0 * dy0 + dz0 * dz0;
        if (d2 < 1.0f) return false;
        float md = Vars.Aimbot.maxDistance;
        if (md >= 50.f && md <= 800.f && d2 > md * md) return false;
        // Visible check: включается через параметр visibleOnly
        if (visibleOnly && !e.visible) return false;
        return true;
    };
    if (lockId) {
        for (auto& e : s_world) {
            if (!e.local && e.id == lockId) { tgt = &e; break; }
        }
        // залоченный протух по фильтру (умер/уснул/стал тиммейтом/ушёл далеко) — отпускаем
        if (tgt && !aimOk(*tgt)) { lockId = 0; lockLost = 0; tgt = nullptr; }
        else if (!tgt) {
            // объект пропал из списка — сразу даём шанс выбрать нового,
            // а не висим 30 тиков на пустом локе
            if (++lockLost > 5) { lockId = 0; lockLost = 0; }
            else return false;
        } else {
            // валидация залоченного: кость обязана быть на экране и в fov*1.5,
            // иначе лок протух (цель ушла за спину/за край) — отпускаем
            V3f laim = boneWorld(*tgt, boneIdx);
            V2f ls;
            bool lok = w2s(s_cam, s_fwd, laim, s_tanHalf, s_aspect,
                           (float)s_sw, (float)s_sh, ls) && onScreen(ls.x, ls.y);
            float ldx = ls.x - cx, ldy = ls.y - cy;
            float ld2 = ldx * ldx + ldy * ldy;
            float lim = fovPx * 1.5f;
            if (!lok || ld2 > lim * lim) {
                if (++lockLost > 8) { lockId = 0; lockLost = 0; tgt = nullptr; }
                else return false;
            } else {
                lockLost = 0;
                // перехват: если другая цель в fov заметно ближе к центру
                // (линейно < 70% от залоченной) — переключаемся на неё,
                // а не тянем вечно первого попавшего в fov
                const Ent* better = nullptr;
                float best2 = ld2 * 0.49f; // 0.7^2
                float cap = fovPx * fovPx;
                if (best2 > cap) best2 = cap;
                for (auto& e : s_world) {
                    if (e.local || e.id == lockId) continue;
                    if (!aimOk(e)) continue; // dead/sleep/team/dist уже проверены
                    float dx0 = e.pos.x - s_cam.x, dy0 = e.pos.y - s_cam.y, dz0 = e.pos.z - s_cam.z;
                    if (dx0 * dx0 + dy0 * dy0 + dz0 * dz0 < 1.0f) continue;
                    V3f am = boneWorld(e, boneIdx);
                    V2f s;
                    if (!w2s(s_cam, s_fwd, am, s_tanHalf, s_aspect, (float)s_sw, (float)s_sh, s))
                        continue;
                    if (!onScreen(s.x, s.y)) continue;
                    float dx = s.x - cx, dy = s.y - cy;
                    float d2 = dx * dx + dy * dy;
                    if (d2 < cap && d2 < best2) { best2 = d2; better = &e; }
                }
                if (better) { tgt = better; lockId = tgt->id; }
            }
        }
    }
    if (!tgt) {
        float best = fovPx * fovPx;
        for (auto& e : s_world) {
            if (e.local || e.dead) continue;
            if (!aimOk(e)) continue; // fixed target safety filters
            float dx0 = e.pos.x - s_cam.x, dy0 = e.pos.y - s_cam.y, dz0 = e.pos.z - s_cam.z;
            if (dx0 * dx0 + dy0 * dy0 + dz0 * dz0 < 1.0f) continue;
            V3f aim = boneWorld(e, boneIdx);
            V2f s;
            if (!w2s(s_cam, s_fwd, aim, s_tanHalf, s_aspect, (float)s_sw, (float)s_sh, s))
                continue;
            if (!onScreen(s.x, s.y)) continue;
            float dx = s.x - cx, dy = s.y - cy;
            float d2 = dx * dx + dy * dy;
            if (d2 < best) { best = d2; tgt = &e; }
        }
        if (tgt) { lockId = tgt->id; lockLost = 0; }
        else {
            return false;
        }
    } else {
        lockLost = 0;
    }
    // локальный игрок: адреса + текущие углы (мёртвый локальный не аимим)
    uint64_t localObj = 0, ml = 0;
    for (auto& e : s_world) {
        if (!e.local || !e.id) continue;
        if (e.dead) return false;
        localObj = e.id;
        break;
    }
    if (!localObj) return false;
    if (!readU64(localObj + kMouseLookOff, ml) || !ml || (ml & 7)) return false;
    float curYawRaw = 0, curLook = 0;
    if (!rpatch::readMem(g_pid, ml + 0x4C + 4, &curYawRaw, 4) || !std::isfinite(curYawRaw)) return false;
    if (!readF32(localObj + kLookAngleOff, curLook) || !std::isfinite(curLook) || fabsf(curLook) > 90.f) return false;
    V3f bw = boneWorld(*tgt, boneIdx);
    float targetSpeed = 0.0f;
    // Estimate horizontal velocity from successive published snapshots and
    // lead the selected bone slightly.  Smooth interpolation alone always
    // trails a player who is running sideways; this compensates that lag
    // without turning the aim into a snap.
    static uint64_t trackId = 0;
    static uint64_t trackSeq = 0;
    static int trackBone = -1;
    static V3f previousBone{0, 0, 0};
    static double previousBoneTime = 0.0;
    const double trackNow = nowSec();
    if (trackId == tgt->id && trackBone == boneIdx &&
        trackSeq != s_snapshotSeq && previousBoneTime > 0.0) {
        const double sampleDt = trackNow - previousBoneTime;
        if (sampleDt > 0.01 && sampleDt < 0.30) {
            V3f velocity{
                (bw.x - previousBone.x) / (float)sampleDt,
                0.0f,
                (bw.z - previousBone.z) / (float)sampleDt
            };
            targetSpeed = sqrtf(velocity.x * velocity.x + velocity.z * velocity.z);
            // Reject a stale/out-of-order snapshot instead of leading into a
            // teleport or a resolver hiccup.
            if (targetSpeed < 0.05f) {
                targetSpeed = 0.0f;
            } else if (targetSpeed > 15.0f) {
                targetSpeed = 15.0f;
                const float scale = 15.0f /
                    sqrtf(velocity.x * velocity.x + velocity.z * velocity.z);
                velocity.x *= scale;
                velocity.z *= scale;
            }
            const float leadTime = fminf(0.12f, 0.025f + targetSpeed * 0.012f);
            bw.x += velocity.x * leadTime;
            bw.z += velocity.z * leadTime;
        }
    }
    trackId = tgt->id;
    trackBone = boneIdx;
    trackSeq = s_snapshotSeq;
    previousBone = boneWorld(*tgt, boneIdx);
    previousBoneTime = trackNow;
    out.targetSpeed = targetSpeed;

    if (boneIdx == 0) {
        // Head is an exact head/look anchor by default.  Keep the existing
        // config binding as an optional explicit offset, but never silently
        // lower the head target into the chest.
        float drop = Vars.Aimbot.headDrop;
        if (!(drop >= 0.f && drop <= 0.6f)) drop = 0.0f;
        bw.y -= drop;
    }
    float vx = bw.x - s_cam.x;
    float vy = bw.y - s_cam.y;
    float vz = bw.z - s_cam.z;
    float len = sqrtf(vx * vx + vy * vy + vz * vz);
    if (len < 1.f) return false;
    float desYaw = atan2f(vx, vz) * 57.29578f;
    float desPitchUp = asinf(vy / len) * 57.29578f;
    if (desPitchUp > 85.f) desPitchUp = 85.f;
    if (desPitchUp < -85.f) desPitchUp = -85.f;
    float desLook = -desPitchUp;
    float yawErr = wrap180(desYaw - wrap180(curYawRaw));
    float pitchErrUp = desPitchUp - (-curLook);
    out.yawAddr = ml + 0x4C + 4;
    out.pitchMirrorAddr = ml + 0x4C;
    out.lookAddr = localObj + kLookAngleOff;
    // lastTickPosition локального игрока (для SilentAim/Magic Bullets)
    out.localPosAddr = localObj + kPosOff;
    out.curYawRaw = curYawRaw;
    out.curLook = curLook;
    out.desYaw = desYaw;
    out.desLook = desLook;
    out.yawErr = yawErr;
    out.pitchErrUp = pitchErrUp;
    // Позиция цели (кость после lead-компенсации): используется SilentAim
    out.targetX = bw.x;
    out.targetY = bw.y;
    out.targetZ = bw.z;
    return true;
}

} // namespace esp

// ─── Экспорт для FreeCam ─────────────────────────────────────────────────────
// FreeCam.cpp использует: extern uintptr_t esp_getLocalPM();
// Возвращает sticky-адрес локального PlayerManager, обновляемый в esp::tick().
uintptr_t esp_getLocalPM() {
    return (uintptr_t)esp::g_sLocalObj;
}
