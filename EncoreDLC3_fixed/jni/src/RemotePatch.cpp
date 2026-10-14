// RemotePatch.cpp — robust READ-ONLY чтение чужой памяти (root, external ELF).
// Проблемы старого кода которые чиним:
//  1) один process_vm_readv на весь len -> EFAULT на границе страниц валил всё чтение.
//  2) нет fallback на /proc/pid/mem (на эмуляторах vm_readv часто режется SELinux).
//  3) pidOf каждый тик сканировал /proc и матчил только точный cmdline.
//  4) строки читались по 32 байта / по 2 байта на символ -> сотни syscall на кадр.
// Здесь: чанки по страницам, fallback pread, кеш pid+memfd, строки за 1 syscall.
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include "RemotePatch.h"
#include <cstdio>
#include <cstring>
#include <cctype>
#include <cerrno>
#include <dirent.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/uio.h>
#include <sys/types.h>
#include <signal.h>
#include <mutex>

namespace rpatch {
namespace {

// --- кеш pid ---
int g_cachedPid = -1;
char g_cachedName[128] = {0};

// Android can leave a zombie entry with a readable cmdline/comm after the
// Unity process has already exited. Such a PID may have an empty maps file.
static bool procUsable(int pid) {
    char path[64];
    snprintf(path, sizeof(path), "/proc/%d/status", pid);
    FILE* f = fopen(path, "r");
    if (!f) return false;
    char line[256];
    bool ok = true;
    while (fgets(line, sizeof(line), f)) {
        if (strncmp(line, "State:", 6) != 0) continue;
        const char* p = line + 6;
        while (*p == ' ' || *p == '\t') p++;
        if (*p == 'Z' || *p == 'X') ok = false; // zombie/dead
        break;
    }
    fclose(f);
    return ok;
}

// Score a matching process. Prefer the actual Unity worker that maps
// libil2cpp.so over a launcher/service with the same package prefix.
static int procMapScore(int pid) {
    char path[64];
    snprintf(path, sizeof(path), "/proc/%d/maps", pid);
    FILE* f = fopen(path, "r");
    if (f) {
        int c = fgetc(f);
        if (c == EOF) { fclose(f); f = nullptr; }
        else ungetc(c, f);
    }
    if (!f) {
        snprintf(path, sizeof(path), "/proc/%d/task/%d/maps", pid, pid);
        f = fopen(path, "r");
    }
    if (!f) return -1;
    bool any = false, il2cpp = false;
    char line[1024];
    while (fgets(line, sizeof(line), f)) {
        any = true;
        if (strstr(line, "libil2cpp.so")) il2cpp = true;
    }
    fclose(f);
    return il2cpp ? 3 : (any ? 1 : 0);
}

bool procNameMatches(int pid, const char* want) {
    // 1) cmdline: первый токен до '\0'
    char path[64];
    snprintf(path, sizeof(path), "/proc/%d/cmdline", pid);
    FILE* f = fopen(path, "r");
    if (f) {
        char cmd[256] = {0};
        size_t n = fread(cmd, 1, sizeof(cmd) - 1, f);
        fclose(f);
        if (n > 0) {
            cmd[n] = '\0';
            // cmdline может быть "pkg\0..." — сравниваем первый токен
            if (strcmp(cmd, want) == 0) return true;
            // префикс-матч (некоторые билды дописывают суффикс)
            size_t wl = strlen(want);
            if (n >= wl && memcmp(cmd, want, wl) == 0 &&
                (cmd[wl] == '\0' || cmd[wl] == ':')) return true;
        }
    }
    // 2) comm: короткое имя (15 символов) — проверяем префикс пакета
    snprintf(path, sizeof(path), "/proc/%d/comm", pid);
    f = fopen(path, "r");
    if (f) {
        char comm[64] = {0};
        if (fgets(comm, sizeof(comm), f)) {
            size_t L = strlen(comm);
            while (L && (comm[L-1] == '\n' || comm[L-1] == '\r')) comm[--L] = '\0';
            // comm обрезан до 15 символов, поэтому только префикс
            if (strncmp(comm, want, L < 15 ? L : 15) == 0 ||
                strncmp(want, comm, L) == 0) {
                // доп. проверка cmdline началом чтобы не схватить чужое
                return true;
            }
        }
        fclose(f);
    }
    return false;
}

// --- кеш mem fd ---
int g_memFd = -1;
int g_memFdPid = -1;
// Snapshot collection now runs concurrently with aim/input and PID checks.
// Serialize the shared /proc fd caches so a process restart cannot close an fd
// while another thread is using it.
std::recursive_mutex g_memMu;

void closeMemFd() {
    if (g_memFd >= 0) { close(g_memFd); g_memFd = -1; g_memFdPid = -1; }
}

int memFdFor(int pid) {
    if (pid <= 0) return -1;
    if (g_memFd >= 0 && g_memFdPid == pid) return g_memFd;
    closeMemFd();
    char path[64];
    snprintf(path, sizeof(path), "/proc/%d/mem", pid);
    int fd = open(path, O_RDONLY | O_CLOEXEC);
    if (fd < 0) return -1;
    g_memFd = fd;
    g_memFdPid = pid;
    return fd;
}

// Один vm_readv чанк (не больше 16К за вызов чтобы не упереться в лимиты).
static ssize_t vmReadOnce(int pid, uintptr_t addr, uint8_t* out, size_t len) {
    struct iovec local;
    struct iovec remote;
    local.iov_base = out;
    local.iov_len = len;
    remote.iov_base = (void*)addr;
    remote.iov_len = len;
    errno = 0;
    return process_vm_readv(pid, &local, 1, &remote, 1, 0);
}

static bool preadOnce(int pid, uintptr_t addr, uint8_t* out, size_t len) {
    int fd = memFdFor(pid);
    if (fd < 0) return false;
    size_t done = 0;
    // pread может вернуть short — добираем
    while (done < len) {
        errno = 0;
        ssize_t r = pread(fd, out + done, len - done, (off_t)(addr + done));
        if (r > 0) { done += (size_t)r; continue; }
        if (r == 0) break; // EOF — дыра в мапах
        if (errno == EINTR) continue;
        break;
    }
    return done == len;
}

// --- кеш mem fd для ЗАПИСИ (O_RDWR, отдельно от read-only) ---
int g_memWFd = -1;
int g_memWFdPid = -1;

void closeMemWFd() {
    if (g_memWFd >= 0) { close(g_memWFd); g_memWFd = -1; g_memWFdPid = -1; }
}

static ssize_t vmWriteOnce(int pid, uintptr_t addr, const uint8_t* data, size_t len) {
    struct iovec local;
    struct iovec remote;
    local.iov_base = (void*)data;
    local.iov_len = len;
    remote.iov_base = (void*)addr;
    remote.iov_len = len;
    errno = 0;
    return process_vm_writev(pid, &local, 1, &remote, 1, 0);
}

static bool pwriteOnce(int pid, uintptr_t addr, const uint8_t* data, size_t len) {
    if (pid <= 0) return false;
    if (g_memWFd < 0 || g_memWFdPid != pid) {
        closeMemWFd();
        char path[64];
        snprintf(path, sizeof(path), "/proc/%d/mem", pid);
        int fd = open(path, O_RDWR | O_CLOEXEC);
        if (fd < 0) return false;
        g_memWFd = fd;
        g_memWFdPid = pid;
    }
    size_t done = 0;
    while (done < len) {
        errno = 0;
        ssize_t r = pwrite(g_memWFd, data + done, len - done, (off_t)(addr + done));
        if (r > 0) { done += (size_t)r; continue; }
        if (r == 0) break;
        if (errno == EINTR) continue;
        break;
    }
    return done == len;
}

} // namespace

bool pidAlive(int pid) {
    if (pid <= 0) return false;
    char path[48];
    snprintf(path, sizeof(path), "/proc/%d/cmdline", pid);
    return access(path, R_OK) == 0;
}

void dropPidCache() {
    std::lock_guard<std::recursive_mutex> lock(g_memMu);
    g_cachedPid = -1;
    g_cachedName[0] = '\0';
    closeMemFd();
    closeMemWFd();
}

int pidOf(const char* processName) {
    std::lock_guard<std::recursive_mutex> lock(g_memMu);
    if (!processName || !*processName) return -1;
    // fast path: кеш жив и всё ещё тот процесс
    if (g_cachedPid > 0 && strcmp(g_cachedName, processName) == 0) {
        if (pidAlive(g_cachedPid) && procUsable(g_cachedPid) &&
            procNameMatches(g_cachedPid, processName))
            return g_cachedPid;
        // протух
        g_cachedPid = -1;
        closeMemFd();
    }
    DIR* d = opendir("/proc");
    if (!d) return -1;
    struct dirent* e;
    int found = -1;
    int bestScore = -2;
    int candidates = 0;
    while ((e = readdir(d))) {
        if (!isdigit((unsigned char)e->d_name[0])) continue;
        int pid = atoi(e->d_name);
        if (pid <= 0 || !procUsable(pid)) continue;
        if (!procNameMatches(pid, processName)) continue;
        candidates++;
        int score = procMapScore(pid);
        // A real mapped process wins over an empty/zombie-like entry. Keep the
        // first candidate only when all map reads are unavailable (-1).
        if (found < 0 || score > bestScore) {
            found = pid;
            bestScore = score;
        }
    }
    closedir(d);
    if (found > 0) {
        g_cachedPid = found;
        snprintf(g_cachedName, sizeof(g_cachedName), "%s", processName);
        if (candidates > 1 || bestScore <= 0) {
            printf("[pid] target=%s candidates=%d selected=%d mapScore=%d\n",
                   processName, candidates, found, bestScore);
            fflush(stdout);
        }
    }
    return found;
}

std::vector<uintptr_t> libBases(int pid, const char* libName) {
    std::vector<uintptr_t> out;
    if (pid <= 0 || !libName) return out;
    char path[64];
    snprintf(path, sizeof(path), "/proc/%d/maps", pid);
    FILE* f = fopen(path, "r");
    if (!f) return out;
    char line[1024];
    while (fgets(line, sizeof(line), f)) {
        if (!strstr(line, libName)) continue;
        unsigned long start = 0, off = 0;
        char perms[8] = {0};
        if (sscanf(line, "%lx-%*lx %7s %lx", &start, perms, &off) != 3) continue;
        if (off != 0 || perms[0] != 'r') continue;
        out.push_back((uintptr_t)start);
    }
    fclose(f);
    return out;
}

uintptr_t libBase(int pid, const char* libName) {
    if (pid <= 0 || !libName) return 0;
    char path[64];
    snprintf(path, sizeof(path), "/proc/%d/maps", pid);
    FILE* f = fopen(path, "r");
    if (!f) return 0;
    char line[1024];
    uintptr_t base = 0;
    while (fgets(line, sizeof(line), f)) {
        if (!strstr(line, libName)) continue;
        base = (uintptr_t)strtoull(line, nullptr, 16);
        break;
    }
    fclose(f);
    return base;
}

// Строгое чтение: режем на куски по 0x1000 (граница страниц — главная причина
// частичных EFAULT), каждый кусок: vm_readv -> fallback pread. Так один битый
// байт не валит соседние страницы, а мелочь (8/4 байта) идёт за 1 syscall.
bool readMem(int pid, uintptr_t addr, void* out, size_t len) {
    std::lock_guard<std::recursive_mutex> lock(g_memMu);
    if (pid <= 0 || !out || len == 0 || addr == 0) return false;
    // выравнивать не обязательно, но кусок не должен пересекать много страниц
    static const size_t kChunk = 0x1000;
    uint8_t* dst = (uint8_t*)out;
    size_t done = 0;
    while (done < len) {
        size_t want = len - done;
        // не рвём страницу без нужды: до конца страницы
        uintptr_t cur = addr + done;
        size_t pageLeft = 0x1000 - (cur & 0xFFF);
        if (want > pageLeft) want = pageLeft;
        if (want > kChunk) want = kChunk;

        ssize_t r = vmReadOnce(pid, cur, dst + done, want);
        if (r == (ssize_t)want) { done += want; continue; }
        if (r > 0) {
            // частичное чтение vm_readv: добиваем остаток pread'ом
            size_t have = (size_t)r;
            if (preadOnce(pid, cur + have, dst + done + have, want - have)) {
                done += want;
                continue;
            }
            return false;
        }
        if (errno == EINTR) continue;
        // vm_readv не смог — пробуем pread сразу весь кусок
        if (preadOnce(pid, cur, dst + done, want)) { done += want; continue; }
        return false;
    }
    return true;
}

// Толерантное чтение для сканеров: идём по страницам 0x1000, битую страницу
// зануляем и идём дальше (а не рвём весь 1МБ чанк). got = len всегда при успехе
// вызова (буфер валиден), return true если битых страниц не было.
bool readChunkTolerant(int pid, uintptr_t addr, uint8_t* out, size_t len, size_t* got) {
    std::lock_guard<std::recursive_mutex> lock(g_memMu);
    if (got) *got = 0;
    if (pid <= 0 || !out || len == 0 || addr == 0) return false;
    static const size_t kPage = 0x1000;
    size_t off = 0;
    bool allOk = true;
    while (off < len) {
        size_t want = len - off;
        if (want > kPage) {
            // ровняем на границу страниц чтобы битая страница не травила соседнюю
            uintptr_t cur = addr + off;
            size_t pageLeft = kPage - (cur & (kPage - 1));
            if (want > pageLeft) want = pageLeft;
        }
        ssize_t r = vmReadOnce(pid, addr + off, out + off, want);
        if (r == (ssize_t)want) { off += want; continue; }
        if (r > 0) {
            // часть страницы прочиталась — остаток добиваем pread
            size_t have = (size_t)r;
            if (preadOnce(pid, addr + off + have, out + off + have, want - have)) {
                off += want;
                continue;
            }
            memset(out + off + have, 0, want - have);
            off += want;
            allOk = false;
            continue;
        }
        if (errno == EINTR) continue;
        if (preadOnce(pid, addr + off, out + off, want)) { off += want; continue; }
        // битая страница: занулить и идти дальше
        memset(out + off, 0, want);
        off += want;
        allOk = false;
    }
    if (got) *got = off;
    return allOk;
}

size_t readPartial(int pid, uintptr_t addr, uint8_t* out, size_t len) {
    size_t got = 0;
    readChunkTolerant(pid, addr, out, len, &got);
    return got;
}

bool readU64(int pid, uintptr_t addr, uint64_t& out) {
    out = 0;
    return readMem(pid, addr, &out, 8);
}
bool readU32(int pid, uintptr_t addr, uint32_t& out) {
    out = 0;
    return readMem(pid, addr, &out, 4);
}
bool readF32(int pid, uintptr_t addr, float& out) {
    out = 0;
    return readMem(pid, addr, &out, 4);
}
bool readU8(int pid, uintptr_t addr, uint8_t& out) {
    out = 0;
    return readMem(pid, addr, &out, 1);
}

// Угловой аим: пишем yaw/pitch локального игрока (data-поля, не .text).
// Куски до 0x1000, каждый: vm_writev -> fallback pwrite в /proc/pid/mem.
bool writeMem(int pid, uintptr_t addr, const void* data, size_t len) {
    std::lock_guard<std::recursive_mutex> lock(g_memMu);
    if (pid <= 0 || !data || len == 0 || addr == 0) return false;
    if (!pidAlive(pid)) { dropPidCache(); return false; }
    const uint8_t* src = (const uint8_t*)data;
    size_t done = 0;
    while (done < len) {
        size_t want = len - done;
        uintptr_t cur = addr + done;
        size_t pageLeft = 0x1000 - (cur & 0xFFF);
        if (want > pageLeft) want = pageLeft;
        ssize_t r = vmWriteOnce(pid, cur, src + done, want);
        if (r == (ssize_t)want) { done += want; continue; }
        if (r > 0) {
            size_t have = (size_t)r;
            if (pwriteOnce(pid, cur + have, src + done + have, want - have)) {
                done += want;
                continue;
            }
            return false;
        }
        if (errno == EINTR) continue;
        if (pwriteOnce(pid, cur, src + done, want)) { done += want; continue; }
        // протухший fd (игра перезапустилась с тем же pid? нет — сбрасываем кеш)
        closeMemWFd();
        return false;
    }
    return true;
}

bool writeU64(int pid, uintptr_t addr, uint64_t val) {
    return writeMem(pid, addr, &val, 8);
}
bool writeF32(int pid, uintptr_t addr, float val) {
    return writeMem(pid, addr, &val, 4);
}

// C-строка за ОДИН readMem (cap обычно 48..64). Старый код дёргал syscall
// на каждые 32 байта + вызывался тысячи раз при скане -> отсюда "плохо читает".
bool readCString(int pid, uintptr_t addr, char* out, size_t cap) {
    if (!out || cap == 0 || addr == 0) return false;
    // cap маленький — читаем целиком за раз
    if (cap <= 512) {
        char tmp[512];
        if (!readMem(pid, addr, tmp, cap)) return false;
        tmp[cap - 1] = '\0';
        // обязательный \0 в пределах cap, иначе это не строка
        size_t L = strnlen(tmp, cap);
        if (L >= cap) return false;
        memcpy(out, tmp, L + 1);
        return true;
    }
    // большой cap — кусками по 256, но останавливаемся на первом \0
    size_t done = 0;
    while (done < cap) {
        size_t want = cap - done;
        if (want > 256) want = 256;
        if (!readMem(pid, addr + done, out + done, want)) return false;
        void* z = memchr(out + done, '\0', want);
        if (z) return true;
        done += want;
    }
    out[cap - 1] = '\0';
    return true;
}

} // namespace rpatch
