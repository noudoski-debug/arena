#pragma once
// RemotePatch.h — чтение + ТОЧЕЧНАЯ запись памяти чужого процесса (external ELF, root).
// Чтение: process_vm_readv + /proc/pid/mem pread. Запись: process_vm_writev + pwrite.
// Пишем только data-поля взгляда (MouseLook yaw / lookAngle), .text не трогаем.
#include <cstdint>
#include <cstddef>
#include <string>
#include <vector>

namespace rpatch {
// --- pid ---
int pidOf(const char* processName);   // кешированный поиск по cmdline+comm, -1 если нет
void dropPidCache();                  // сбросить кеш (при смене игры)
bool pidAlive(int pid);               // жив ли pid

// --- либы ---
uintptr_t libBase(int pid, const char* libName);
std::vector<uintptr_t> libBases(int pid, const char* libName);

// --- базовое чтение ---
// Строгое: true только если прочитано ровно len байт.
bool readMem(int pid, uintptr_t addr, void* out, size_t len);
// --- запись (угловой аим пишет yaw/pitch локального игрока) ---
// process_vm_writev (fast path) + pwrite в /proc/pid/mem (fallback). Только data-поля.
bool writeMem(int pid, uintptr_t addr, const void* data, size_t len);
// Толерантное (для сканеров): читает постранично, битые страницы зануляет,
// возвращает число реально прочитанных-валидных байт через got.
// Возвращает true если got == len, иначе false, но got всё равно заполнен.
bool readChunkTolerant(int pid, uintptr_t addr, uint8_t* out, size_t len, size_t* got);
// Сколько байт удалось прочитать (0..len), битые страницы пропускаются.
size_t readPartial(int pid, uintptr_t addr, uint8_t* out, size_t len);

// --- быстрые helpers (по 1 syscall на вызов) ---
bool readU64(int pid, uintptr_t addr, uint64_t& out);
bool readU32(int pid, uintptr_t addr, uint32_t& out);
bool readF32(int pid, uintptr_t addr, float& out);
bool readU8(int pid, uintptr_t addr, uint8_t& out);
// C-строка за 1 read: читает cap байт одним махом, ставит \0.
bool readCString(int pid, uintptr_t addr, char* out, size_t cap);
// --- быстрые helpers записи ---
bool writeU64(int pid, uintptr_t addr, uint64_t val);
bool writeF32(int pid, uintptr_t addr, float val);
} // namespace rpatch
