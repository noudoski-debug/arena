// p67 — ptrace-инжектор для 64-битного Android (arm64-v8a/x86_64, root).
// Использование: p67 <pid> /data/local/tmp/67/libaudio.so [memfd-name]
// Грузит .so в процесс через remote mmap + remote dlopen.
// Сначала пробует memfd (в maps без пути к файлу), иначе путь к файлу.
// Только ptrace + process_vm_writev, без остановки мира (аттач к одному потоку).
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdint>
#include <string>
#include <vector>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <dirent.h>
#include <dlfcn.h>
#include <sys/ptrace.h>
#include <sys/wait.h>
#include <sys/uio.h>
#if !defined(__aarch64__)
#include <sys/user.h>
#endif
#include <sys/mman.h>
#include <elf.h>

#if defined(__aarch64__)
// Android exposes AArch64 general registers through GETREGSET/NT_PRSTATUS.
// Keep a local layout so this helper also builds with NDKs that do not export
// struct user_pt_regs from <sys/user.h>.
#ifndef NT_PRSTATUS
#define NT_PRSTATUS 1
#endif
struct Arm64Regs {
    uint64_t regs[31];
    uint64_t sp;
    uint64_t pc;
    uint64_t pstate;
};
#endif

static int g_pid = -1;

struct MapEnt { uintptr_t s, e, off; std::string path, perms; };

static std::vector<MapEnt> parseMaps(int pid) {
    std::vector<MapEnt> v;
    char p[64];
    snprintf(p, sizeof(p), "/proc/%d/maps", pid);
    FILE* f = fopen(p, "r");
    if (!f) return v;
    char line[1024];
    while (fgets(line, sizeof(line), f)) {
        unsigned long s = 0, e = 0, o = 0;
        char pm[8] = {0};
        if (sscanf(line, "%lx-%lx %7s %lx", &s, &e, pm, &o) != 4) continue;
        char* sp = strchr(line, '/');
        if (!sp) {
            if (!strstr(line, "00:00 0")) continue;
        }
        MapEnt m{(uintptr_t)s, (uintptr_t)e, (uintptr_t)o, sp ? sp : "[anon]", pm};
        size_t n = m.path.size();
        while (n && (m.path[n-1] == '\n' || m.path[n-1] == ' ')) m.path.resize(--n);
        v.push_back(m);
    }
    fclose(f);
    return v;
}

// база мэппинга с file off 0 для ТОЧНОГО пути (а не подстроки)
static uintptr_t libBaseExact(const std::vector<MapEnt>& ms, const std::string& path) {
    for (auto& m : ms)
        if (m.off == 0 && m.path == path) return m.s;
    return 0;
}

// путь файла + file-offset адреса (addr-start+off мэппинга)
static bool addrFileOff(const std::vector<MapEnt>& ms, uintptr_t addr,
                        std::string& path, uintptr_t& foff) {
    for (auto& m : ms) {
        if (addr >= m.s && addr < m.e && !m.path.empty() && m.path[0] == '/') {
            path = m.path;
            foff = addr - m.s + m.off;
            return true;
        }
    }
    return false;
}

// runtime-адрес в таргете по (путь,fileoff); для кода только r-xp
static uintptr_t remoteAddr(const std::vector<MapEnt>& tms,
                            const std::string& path, uintptr_t foff, bool needX) {
    for (auto& m : tms) {
        if (m.path != path) continue;
        if (needX && m.perms.find('x') == std::string::npos) continue;
        if (foff >= m.off && foff < m.off + (m.e - m.s))
            return m.s + (foff - m.off);
    }
    return 0;
}

// оффсет адреса относительно базы его мэппинга (start - fileoff)
static uintptr_t toOffset(const std::vector<MapEnt>& ms, uintptr_t addr) {
    for (auto& m : ms) {
        if (addr >= m.s && addr < m.e) return addr - (m.s - m.off);
    }
    return 0;
}

static bool vmWrite(int pid, uintptr_t dst, const void* src, size_t n) {
    struct iovec l{(void*)src, n}, r{(void*)dst, n};
    size_t done = 0;
    while (done < n) {
        struct iovec ll{(char*)src + done, n - done}, rr{(void*)(dst + done), n - done};
        ssize_t w = process_vm_writev(pid, &ll, 1, &rr, 1, 0);
        if (w <= 0) return false;
        done += (size_t)w;
    }
    return true;
}

// вызов func(args...) в потоке pid. Возврат x0/RAX. -1UL при ошибке.
static uint64_t rcall(const uint64_t* args, int nargs, uintptr_t func, unsigned timeout_s = 12) {
#if defined(__aarch64__)
    Arm64Regs oldr{}, recr{};
    struct iovec oldIo{&oldr, sizeof(oldr)};
    if (ptrace(PTRACE_GETREGSET, g_pid, (void*)NT_PRSTATUS, &oldIo) != 0) {
        printf("[p67] getregs: %s\n", strerror(errno));
        return (uint64_t)-1;
    }
    recr = oldr;
    // AArch64 passes the first eight integer arguments in x0..x7.
    for (int i = 0; i < nargs && i < 8; i++) recr.regs[i] = args[i];
    recr.pc = (uint64_t)func;
    recr.regs[30] = 0; // return into address zero -> a controllable stop
    recr.sp = (recr.sp - 0x100) & ~0xFULL; // AAPCS64 requires 16-byte alignment
    struct iovec setIo{&recr, sizeof(recr)};
    if (ptrace(PTRACE_SETREGSET, g_pid, (void*)NT_PRSTATUS, &setIo) != 0) {
        printf("[p67] setregs: %s\n", strerror(errno));
        return (uint64_t)-1;
    }
#else
    struct user_regs_struct oldr{}, recr{};
    if (ptrace(PTRACE_GETREGS, g_pid, 0, &oldr) != 0) {
        printf("[p67] getregs: %s\n", strerror(errno));
        return (uint64_t)-1;
    }
    recr = oldr;
    if (nargs > 0) recr.rdi = args[0];
    if (nargs > 1) recr.rsi = args[1];
    if (nargs > 2) recr.rdx = args[2];
    if (nargs > 3) recr.rcx = args[3];
    if (nargs > 4) recr.r8 = args[4];
    if (nargs > 5) recr.r9 = args[5];
    recr.rip = func;
    recr.rsp -= (128 + 8); // red zone x86_64 (128) + слот адреса возврата
    uint64_t zero = 0;
    if (!vmWrite(g_pid, recr.rsp, &zero, 8)) {
        printf("[p67] stack write fail\n");
        return (uint64_t)-1;
    }
    recr.rax = 0;
    if (ptrace(PTRACE_SETREGS, g_pid, 0, &recr) != 0) {
        printf("[p67] setregs: %s\n", strerror(errno));
        return (uint64_t)-1;
    }
#endif
    if (ptrace(PTRACE_CONT, g_pid, 0, 0) != 0) {
        printf("[p67] cont: %s\n", strerror(errno));
        return (uint64_t)-1;
    }
    for (unsigned t = 0; t < timeout_s * 10; t++) {
        int st = 0;
        int w = waitpid(g_pid, &st, WNOHANG);
        if (w == g_pid && WIFSTOPPED(st)) {
            int sig = WSTOPSIG(st);
            if (sig == SIGSEGV || sig == SIGILL || sig == SIGTRAP) {
#if defined(__aarch64__)
                Arm64Regs rr{};
                struct iovec getIo{&rr, sizeof(rr)};
                ptrace(PTRACE_GETREGSET, g_pid, (void*)NT_PRSTATUS, &getIo);
                uint64_t ret = rr.regs[0];
                struct iovec restoreIo{&oldr, sizeof(oldr)};
                ptrace(PTRACE_SETREGSET, g_pid, (void*)NT_PRSTATUS, &restoreIo);
#else
                struct user_regs_struct rr{};
                ptrace(PTRACE_GETREGS, g_pid, 0, &rr);
                uint64_t ret = rr.rax;
                ptrace(PTRACE_SETREGS, g_pid, 0, &oldr);
#endif
                return ret;
            }
            ptrace(PTRACE_CONT, g_pid, 0, sig);
        }
        usleep(100000);
    }
    printf("[p67] rcall timeout, restoring\n");
#if defined(__aarch64__)
    struct iovec restoreIo{&oldr, sizeof(oldr)};
    ptrace(PTRACE_SETREGSET, g_pid, (void*)NT_PRSTATUS, &restoreIo);
#else
    ptrace(PTRACE_SETREGS, g_pid, 0, &oldr);
#endif
    return (uint64_t)-1;
}

int main(int argc, char** argv) {
    if (argc < 3) { printf("usage: p67 <pid> <lib.so> [memfd-name]\n"); return 1; }
    g_pid = atoi(argv[1]);
    const char* lib = argv[2];
    const char* mname = argc > 3 ? argv[3] : "audio_cache";
    printf("[p67] target=%d lib=%s\n", g_pid, lib);

    // свои оффсеты
    auto selfMaps = parseMaps(getpid());
    uint64_t aMmap = (uint64_t)(void*)mmap;
    uint64_t aDlopen = (uint64_t)(void*)dlopen;
    void* hmem = dlsym(RTLD_DEFAULT, "memfd_create");
    uint64_t aMemfd = hmem ? (uint64_t)hmem : 0;
    uint64_t aWrite = (uint64_t)(void*)write;
    uintptr_t offMmap = toOffset(selfMaps, aMmap);
    uintptr_t offDlopen = toOffset(selfMaps, aDlopen);
    uintptr_t offMemfd = hmem ? toOffset(selfMaps, aMemfd) : 0;
    uintptr_t offWrite = toOffset(selfMaps, aWrite);
    if (!offMmap || !offDlopen || !offWrite) { printf("[p67] self offsets fail\n"); return 1; }
    printf("[p67] off mmap=0x%zx dlopen=0x%zx write=0x%zx memfd=0x%zx\n",
           (size_t)offMmap, (size_t)offDlopen, (size_t)offWrite, (size_t)offMemfd);

    // таргет: те же файлы что у нас (один девайс) — адреса по (путь,fileoff)
    auto tMaps = parseMaps(g_pid);
    if (tMaps.empty()) { printf("[p67] no target maps (need root)\n"); return 1; }
    std::string pMmap, pDlopen, pWrite, pMemfd;
    uintptr_t fMmap = 0, fDlopen = 0, fWrite = 0, fMemfd = 0;
    if (!addrFileOff(selfMaps, aMmap, pMmap, fMmap) ||
        !addrFileOff(selfMaps, aDlopen, pDlopen, fDlopen) ||
        !addrFileOff(selfMaps, aWrite, pWrite, fWrite)) {
        printf("[p67] self fileoff fail\n");
        return 1;
    }
    if (hmem && !addrFileOff(selfMaps, aMemfd, pMemfd, fMemfd)) hmem = nullptr;
    uintptr_t rMmap = remoteAddr(tMaps, pMmap, fMmap, true);
    uintptr_t rWrite = remoteAddr(tMaps, pWrite, fWrite, true);
    uintptr_t rDlopen = remoteAddr(tMaps, pDlopen, fDlopen, true);
    uintptr_t rMemfd = hmem ? remoteAddr(tMaps, pMemfd, fMemfd, true) : 0;
    if (!rMmap || !rWrite || !rDlopen) {
        printf("[p67] remote addr fail mmap=0x%zx write=0x%zx dlopen=0x%zx\n",
               (size_t)rMmap, (size_t)rWrite, (size_t)rDlopen);
        return 1;
    }
    printf("[p67] remote mmap=0x%zx dlopen=0x%zx write=0x%zx memfd=0x%zx\n",
           (size_t)rMmap, (size_t)rDlopen, (size_t)rWrite, (size_t)rMemfd);

    if (ptrace(PTRACE_ATTACH, g_pid, 0, 0) != 0) { printf("[p67] attach: %s\n", strerror(errno)); return 1; }
    int st = 0;
    waitpid(g_pid, &st, 0);
    printf("[p67] attached\n");

    uint64_t ret = (uint64_t)-1;
    // remote mmap под буфер пути
    {
        uint64_t a[6] = {0, 16384, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, (uint64_t)-1, 0};
        ret = rcall(a, 6, rMmap);
    }
    if (!ret || ret == (uint64_t)-1 || (int64_t)ret < 0) {
        printf("[p67] remote mmap fail ret=0x%llx\n", (unsigned long long)ret);
        ptrace(PTRACE_DETACH, g_pid, 0, 0);
        return 1;
    }
    uintptr_t rbuf = (uintptr_t)ret;
    printf("[p67] rbuf=0x%zx\n", (size_t)rbuf);

    char loadPath[256] = {0};
    if (rMemfd) {
        // memfd + заливка тела либы через remote write
        FILE* f = fopen(lib, "rb");
        if (!f) { printf("[p67] open lib fail\n"); ptrace(PTRACE_DETACH, g_pid, 0, 0); return 1; }
        fseek(f, 0, SEEK_END);
        long sz = ftell(f);
        fseek(f, 0, SEEK_SET);
        printf("[p67] lib size=%ld\n", sz);
        if (sz <= 0 || sz > 4 * 1024 * 1024) { printf("[p67] bad size\n"); fclose(f); ptrace(PTRACE_DETACH, g_pid, 0, 0); return 1; }
        if (!vmWrite(g_pid, rbuf, mname, strlen(mname) + 1)) { printf("[p67] name write fail\n"); fclose(f); ptrace(PTRACE_DETACH, g_pid, 0, 0); return 1; }
        uint64_t a[6] = {rbuf, 0, 0, 0, 0, 0};
        uint64_t mfd = rcall(a, 2, rMemfd);
        printf("[p67] memfd=%lld\n", (long long)(int64_t)mfd);
        if ((int64_t)mfd < 0) {
            printf("[p67] memfd_create fail, fallback to file\n");
            fclose(f);
            snprintf(loadPath, sizeof(loadPath), "%s", lib);
        } else {
            static uint8_t chunk[8192];
            long done = 0;
            bool ok = true;
            while (done < sz) {
                size_t n = fread(chunk, 1, sizeof(chunk), f);
                if (!n) break;
                if (!vmWrite(g_pid, rbuf + 4096, chunk, n)) { ok = false; break; }
                uint64_t w[6] = {(uint64_t)mfd, rbuf + 4096, n, 0, 0, 0};
                uint64_t wr = rcall(w, 3, rWrite, 20);
                if ((int64_t)wr != (int64_t)n) { printf("[p67] remote write %lld/%u\n", (long long)(int64_t)wr, (unsigned)n); ok = false; break; }
                done += n;
            }
            fclose(f);
            if (!ok || done != sz) { printf("[p67] upload fail\n"); ptrace(PTRACE_DETACH, g_pid, 0, 0); return 1; }
            snprintf(loadPath, sizeof(loadPath), "/proc/self/fd/%lld", (long long)(int64_t)mfd);
            printf("[p67] uploaded %ld as %s\n", sz, loadPath);
        }
    } else {
        snprintf(loadPath, sizeof(loadPath), "%s", lib);
    }
    if (!vmWrite(g_pid, rbuf, loadPath, strlen(loadPath) + 1)) { printf("[p67] path write fail\n"); ptrace(PTRACE_DETACH, g_pid, 0, 0); return 1; }
    {
        uint64_t a[6] = {rbuf, RTLD_NOW, 0, 0, 0, 0};
        uint64_t h = rcall(a, 2, rDlopen, 20);
        printf("[p67] dlopen -> 0x%llx\n", (unsigned long long)h);
        if (!h || h == (uint64_t)-1) {
            printf("[p67] dlopen FAILED\n");
            ptrace(PTRACE_DETACH, g_pid, 0, 0);
            return 1;
        }
    }
    ptrace(PTRACE_DETACH, g_pid, 0, 0);
    // detach обязан отпустить поток — проверяем СОСТОЯНИЕ ЗАДАЧИ (не процесса)
    usleep(300000);
    {
        char stp[64];
        snprintf(stp, sizeof(stp), "/proc/%d/task/%d/status", g_pid, g_pid);
        FILE* sf = fopen(stp, "r");
        char tstate[64] = "?";
        if (sf) {
            char ln[256];
            while (fgets(ln, sizeof(ln), sf)) {
                if (strncmp(ln, "State:", 6) == 0) {
                    strncpy(tstate, ln + 6, sizeof(tstate) - 1);
                    break;
                }
            }
            fclose(sf);
        }
        printf("[p67] thread state after detach:%s", tstate);
        if (strchr(tstate, 't') || strchr(tstate, 'T')) {
            printf("[p67] STILL STOPPED, cont...\n");
            ptrace(PTRACE_CONT, g_pid, 0, 0);
            usleep(300000);
            ptrace(PTRACE_DETACH, g_pid, 0, 0);
        }
    }
    printf("[p67] OK injected %s\n", loadPath);
    return 0;
}
