#pragma once
// Bootstrap diagnostics must work before ImGui/Unity and without logcat visibility.
#include "Target.h"
#include <android/log.h>
#include <cerrno>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <time.h>
#include <unistd.h>

namespace diagnostics {
inline char* filePath() { static char path[512]{}; return path; }
inline void log(const char* format, ...) {
    const int saved=errno;
    char message[640]{};
    va_list args;va_start(args,format);std::vsnprintf(message,sizeof(message),format,args);va_end(args);
    timespec now{};clock_gettime(CLOCK_MONOTONIC,&now);
    char line[768]{};
    const int count=std::snprintf(line,sizeof(line),"%lld.%03ld pid=%d tid=%ld %s\n",
        static_cast<long long>(now.tv_sec),now.tv_nsec/1000000,getpid(),syscall(SYS_gettid),message);
    __android_log_print(ANDROID_LOG_INFO,"EncoreDLC","%s",message);
    if (filePath()[0] && count>0) {
        const int fd=open(filePath(),O_WRONLY|O_CREAT|O_APPEND|O_CLOEXEC|O_NOFOLLOW,0600);
        if (fd>=0) {
            struct stat info{};
            if (fstat(fd,&info)==0 && S_ISREG(info.st_mode) && info.st_size<1024*1024) {
                const size_t length=static_cast<size_t>(count)<sizeof(line)?static_cast<size_t>(count):sizeof(line)-1;
                // One O_APPEND write per event; no mutex held across loader/driver calls.
                ssize_t result;
                do {result=write(fd,line,length);}while(result<0 && errno==EINTR);
            }
            close(fd);
        }
    }
    errno=saved;
}
inline void begin() {
#ifdef __ANDROID__
    // Android UID = user * 100000 + app id. No shared/external storage is used.
    char directory[448]{};
    std::snprintf(directory,sizeof(directory),"/data/user/%u/%s/files/encoredlc",
                  static_cast<unsigned>(getuid()/100000),target::Package);
    if (mkdir(directory,0700)!=0 && errno!=EEXIST) {
        log("BOOT file log unavailable: mkdir errno=%d",errno);return;
    }
    std::snprintf(filePath(),512,"%s/startup.log",directory);
    const int fd=open(filePath(),O_WRONLY|O_CREAT|O_TRUNC|O_CLOEXEC|O_NOFOLLOW,0600);
    if (fd<0) { const int error=errno;filePath()[0]=0;log("BOOT file log unavailable: open errno=%d",error);return; }
    close(fd);
#endif
    log("BOOT diagnostic-bootstrap-v1 built %s %s; file=%s",__DATE__,__TIME__,filePath()[0]?filePath():"unavailable");
}
#ifdef ENCORE_DIAGNOSTICS_TEST
inline void testFile(const char* path) {std::snprintf(filePath(),512,"%s",path);}
#endif
}
