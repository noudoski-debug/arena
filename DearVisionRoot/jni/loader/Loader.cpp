// ARM64 root loader. File-backed dlopen only: no memfd disguises, patches to
// security policy, persistence, or external gameplay/memory-scanning backend.
#include "Maps.h"
#include "Target.h"
#include <elf.h>
#include <dlfcn.h>
#include <sys/ptrace.h>
#include <sys/wait.h>
#include <sys/uio.h>
#include <sys/mman.h>
#include <unistd.h>
#include <signal.h>
#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <chrono>
#include <limits.h>

#if !defined(__aarch64__)
#error "dear-loader must be built for arm64-v8a"
#endif
namespace {
using Clock = std::chrono::steady_clock;
struct Registers { uint64_t x[31], sp, pc, pstate; };
struct FpRegisters { __uint128_t v[32]; uint32_t fpsr, fpcr, reserved[2]; };
std::vector<loader::Mapping> maps(int pid) {
    char path[64]; std::snprintf(path, sizeof(path), "/proc/%d/maps", pid);
    FILE* f=std::fopen(path, "r");
    if (!f) return {};
    std::vector<loader::Mapping> result;
    char line[4096];
    while (std::fgets(line, sizeof(line), f)) {
        loader::Mapping m;
        if (loader::parseMapping(line,m)) result.push_back(m);
    }
    std::fclose(f); return result;
}
bool arm64Elf(const char* path, bool shared) {
    FILE* f=std::fopen(path,"rb");
    if (!f) return false;
    Elf64_Ehdr e{};
    bool ok=std::fread(&e,1,sizeof(e),f)==sizeof(e);
    std::fclose(f);
    return ok && !std::memcmp(e.e_ident,ELFMAG,SELFMAG) && e.e_ident[EI_CLASS]==ELFCLASS64 &&
           e.e_ident[EI_DATA]==ELFDATA2LSB && e.e_machine==EM_AARCH64 && (!shared || e.e_type==ET_DYN);
}
bool targetProcess(int pid) {
    char path[64], name[256]{};
    std::snprintf(path,sizeof(path),"/proc/%d/cmdline",pid);
    FILE* f=std::fopen(path,"rb");
    if (!f) return false;
    std::fread(name,1,sizeof(name)-1,f); std::fclose(f);
    return std::strcmp(name,target::Package)==0;
}
bool writeRemote(int pid, uintptr_t address, const void* data, size_t size) {
    size_t done=0;
    while (done<size) {
        iovec local{const_cast<char*>(static_cast<const char*>(data))+done,size-done};
        iovec remote{reinterpret_cast<void*>(address+done),size-done};
        const ssize_t n=process_vm_writev(pid,&local,1,&remote,1,0);
        if (n<0 && errno==EINTR) continue;
        if (n<=0) return false;
        done+=static_cast<size_t>(n);
    }
    return true;
}
// Scoped attachment: restore CPU registers and detach on every stopped error path.
// Calling a function on a paused application thread can still deadlock on its own
// locks. This helper is experimental; a timeout requires restarting the game.
class Session {
    int pid_;
    bool attached_=false, stopped_=false, saved_=false, closed_=false;
    Registers original_{};
    FpRegisters fp_{};
    bool get(int note, void* out, size_t size) {
        iovec io{out,size};
        return ptrace(PTRACE_GETREGSET,pid_,reinterpret_cast<void*>(static_cast<uintptr_t>(note)),&io)==0 && io.iov_len==size;
    }
    bool set(int note, void* data, size_t size) {
        iovec io{data,size};
        return ptrace(PTRACE_SETREGSET,pid_,reinterpret_cast<void*>(static_cast<uintptr_t>(note)),&io)==0;
    }
    int wait(unsigned milliseconds) {
        const auto deadline=Clock::now()+std::chrono::milliseconds(milliseconds);
        while (Clock::now()<deadline) {
            int status=0;
            const int got=waitpid(pid_,&status,WNOHANG|__WALL);
            if (got==pid_) {
                if (WIFSTOPPED(status)) { stopped_=true; return WSTOPSIG(status); }
                if (WIFEXITED(status)||WIFSIGNALED(status)) { attached_=false; return -1; }
            } else if (got<0 && errno!=EINTR) return -1;
            usleep(10000);
        }
        return 0;
    }
public:
    explicit Session(int pid):pid_(pid) {}
    ~Session() { if (!closed_) close(); }
    bool attach() {
        if (ptrace(PTRACE_SEIZE,pid_,nullptr,nullptr)!=0) return false;
        attached_=true;
        if (ptrace(PTRACE_INTERRUPT,pid_,nullptr,nullptr)!=0 || wait(3000)<=0) return false;
        saved_=get(NT_PRSTATUS,&original_,sizeof(original_)) && get(NT_FPREGSET,&fp_,sizeof(fp_));
        return saved_;
    }
    bool call(uintptr_t function, const std::vector<uint64_t>& args, uint64_t& result) {
        if (!saved_ || !stopped_ || !function || args.size()>8) return false;
        Registers r=original_;
        for (size_t i=0;i<args.size();++i) r.x[i]=args[i];
        r.pc=function; r.x[30]=0; r.sp=(original_.sp-256)&~uint64_t(15);
        if (!set(NT_PRSTATUS,&r,sizeof(r)) || ptrace(PTRACE_CONT,pid_,nullptr,nullptr)!=0) return false;
        stopped_=false;
        const int signal=wait(12000);
        if (signal<=0) {
            std::fprintf(stderr,"Remote call timed out/exited. Restart the game before retrying.\n");
            return false;
        }
        if (!get(NT_PRSTATUS,&r,sizeof(r)) || signal!=SIGSEGV || r.pc!=0) {
            std::fprintf(stderr,"Unexpected remote stop (signal=%d, pc=0x%llx); not a successful return.\n",signal,static_cast<unsigned long long>(r.pc));
            return false;
        }
        result=r.x[0];
        return set(NT_PRSTATUS,&original_,sizeof(original_)) && set(NT_FPREGSET,&fp_,sizeof(fp_));
    }
    bool close() {
        closed_=true;
        if (!attached_) return true;
        if (!stopped_) {
            if (ptrace(PTRACE_INTERRUPT,pid_,nullptr,nullptr)!=0 || wait(3000)<=0) {
                std::fprintf(stderr,"ERROR: cannot stop/restore target thread. Restart game.\n"); return false;
            }
        }
        bool ok=true;
        if (saved_) {
            bool general=set(NT_PRSTATUS,&original_,sizeof(original_));
            bool floating=set(NT_FPREGSET,&fp_,sizeof(fp_));
            ok=general && floating;
        }
        const bool detached=ptrace(PTRACE_DETACH,pid_,nullptr,nullptr)==0;
        attached_=!detached;
        if (!ok || !detached) std::fprintf(stderr,"ERROR: register restoration/detach failed. Restart game.\n");
        return ok && detached;
    }
};
}

int main(int argc,char** argv) {
    if (argc!=3 || geteuid()!=0) {
        std::fprintf(stderr,"Usage (root): dear-loader <pid> </absolute/path/libDear.so>\n"); return 1;
    }
    char* end=nullptr; errno=0;
    const long parsed=std::strtol(argv[1],&end,10);
    if (errno || !end || *end || parsed<=1 || parsed>INT_MAX || parsed==getpid()) return 2;
    const int pid=static_cast<int>(parsed);
    char library[PATH_MAX];
    if (!realpath(argv[2],library) || !arm64Elf(library,true) || !targetProcess(pid)) {
        std::fprintf(stderr,"Invalid library ELF, path, or target package.\n"); return 2;
    }
    char executable[64]; std::snprintf(executable,sizeof(executable),"/proc/%d/exe",pid);
    if (!arm64Elf(executable,false)) { std::fprintf(stderr,"Target is not ARM64.\n"); return 2; }
    const auto local=maps(getpid()), remote=maps(pid);
    for (const auto& m:remote) {
        const size_t slash=m.path.find_last_of('/');
        if (m.path.substr(slash==std::string::npos?0:slash+1)=="libDear.so") {
            std::fprintf(stderr,"libDear.so is already loaded; restart the game to replace it.\n"); return 3;
        }
    }
    auto address=[&](const char* name) {
        return loader::relocate(local,remote,reinterpret_cast<uintptr_t>(dlsym(RTLD_DEFAULT,name)));
    };
    const uintptr_t mmapFn=address("mmap"), dlopenFn=address("dlopen"), munmapFn=address("munmap");
    const uintptr_t dlerrorFn=address("dlerror");
    if (!mmapFn || !dlopenFn || !munmapFn) {
        std::fprintf(stderr,"Cannot resolve identical executable system-library mappings.\n"); return 4;
    }
    Session session(pid);
    if (!session.attach()) { std::fprintf(stderr,"ptrace/register access denied: %s\n",std::strerror(errno)); return 5; }
    const uint64_t size=16384;
    uint64_t buffer=0, handle=0, ignored=0;
    if (!session.call(mmapFn,{0,size,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS,uint64_t(-1),0},buffer) ||
        !buffer || buffer==uint64_t(-1)) return 6;
    if (std::strlen(library)+1>=size || !writeRemote(pid,buffer,library,std::strlen(library)+1)) {
        session.call(munmapFn,{buffer,size},ignored);
        std::fprintf(stderr,"Cannot write remote library path.\n"); return 6;
    }
    if (!session.call(dlopenFn,{buffer,RTLD_NOW|RTLD_LOCAL},handle)) return 7;
    if (!handle) {
        uint64_t error=0;
        if (dlerrorFn && session.call(dlerrorFn,{},error) && error) {
            char message[512]{};
            iovec l{message,sizeof(message)-1}, r{reinterpret_cast<void*>(error),sizeof(message)-1};
            if (process_vm_readv(pid,&l,1,&r,1,0)>0) std::fprintf(stderr,"dlopen: %s\n",message);
        }
        session.call(munmapFn,{buffer,size},ignored);
        std::fprintf(stderr,"dlopen failed. Check file access, linker namespace and SELinux; none are bypassed.\n");
        return 7;
    }
    const bool released=session.call(munmapFn,{buffer,size},ignored) && ignored==0;
    const bool restored=session.close();
    if (!released || !restored) return 8;
    std::printf("dlopen succeeded. Menu-only startup; game hooks require Connect game in the overlay.\n");
    std::printf("This does not confirm renderer initialization or compatibility. Check: logcat -s DearVision\n");
    return 0;
}
