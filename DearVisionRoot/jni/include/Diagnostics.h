#pragma once
#include <android/log.h>
#include <cstdarg>
#include <cstdio>
#include <fcntl.h>
#include <unistd.h>
#include <time.h>

namespace diagnostics {
// Same Android user-0 private files directory used by profiles. No policy changes.
inline constexpr const char* Path = "/data/user/0/com.catsbit.oxidesurvivalisland/files/dearvision-startup.log";
inline void reset() {
#ifdef __ANDROID__
    const int fd=::open(Path,O_WRONLY|O_CREAT|O_TRUNC|O_CLOEXEC,0600);
    if (fd>=0) ::close(fd);
#endif
}
inline void log(const char* format,...) {
    char text[512];
    va_list args;va_start(args,format);
    std::vsnprintf(text,sizeof(text),format,args);va_end(args);
    __android_log_print(ANDROID_LOG_INFO,"DearVision","%s",text);
#ifdef __ANDROID__
    // One unbuffered append per startup event survives ordinary process death.
    // Best effort only: private files may be unavailable (e.g. another Android user).
    char line[640];struct timespec now{};::clock_gettime(CLOCK_REALTIME,&now);
    const int size=std::snprintf(line,sizeof(line),"%lld.%03ld pid=%d %s\n",
        static_cast<long long>(now.tv_sec),now.tv_nsec/1000000,static_cast<int>(::getpid()),text);
    const int fd=::open(Path,O_WRONLY|O_CREAT|O_APPEND|O_CLOEXEC,0600);
    if (fd>=0) {
        if (size>0) { const auto ignored=::write(fd,line,static_cast<size_t>(size)<sizeof(line)?size:sizeof(line)-1);(void)ignored; }
        ::close(fd);
    }
#endif
}
}
