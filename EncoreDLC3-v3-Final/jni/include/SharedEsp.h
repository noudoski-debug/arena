#pragma once
// SharedEsp.h — общий формат esp.bin между internal-collector (в игре)
// и external-overlay (рут, рисует + аим). Никаких сканов памяти снаружи.
#include <cstdint>

namespace shared67 {

static const uint32_t kMagic = 0x36374553u; // '67ES'
static const uint32_t kVersion = 2;
static const int kMaxEnts = 64;
static const int kNameLen = 32;
static const char kPath[] = "/data/local/tmp/67/esp.bin";
static const char kTmpPath[] = "/data/local/tmp/67/esp.tmp";

struct Ent {
    float x, y, z;          // lastTickPosition
    float yaw, pitch;       // только у local (для камеры), у остальных 0
    uint8_t isLocal;        // 1 у локального
    uint8_t _pad[3];
    char name[kNameLen];    // utf8, обрезано
};

struct Frame {
    uint32_t magic;         // kMagic
    uint32_t version;       // kVersion
    uint32_t seq;           // растёт каждый кадр
    uint32_t count;         // ents заполнено
    Ent ents[kMaxEnts];
};

} // namespace shared67
