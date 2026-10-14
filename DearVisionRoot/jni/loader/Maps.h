#pragma once
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>
namespace loader {
struct Mapping { uintptr_t begin=0, end=0, offset=0; std::string permissions, path; };
inline bool parseMapping(const char* line, Mapping& out) {
    unsigned long begin=0, end=0, offset=0;
    char permissions[5]{};
    int consumed=0;
    if (std::sscanf(line, "%lx-%lx %4s %lx %*s %*s %n", &begin, &end, permissions, &offset, &consumed) != 4 ||
        consumed <= 0 || end <= begin) return false;
    std::string path(line+consumed);
    while (!path.empty() && (path.back()=='\n' || path.back()=='\r')) path.pop_back();
    out = {begin, end, offset, permissions, path};
    return true;
}
inline uintptr_t relocate(const std::vector<Mapping>& local, const std::vector<Mapping>& remote, uintptr_t address) {
    for (const auto& source : local) {
        if (address < source.begin || address >= source.end || source.path.empty() || source.path[0] != '/') continue;
        const uintptr_t fileOffset = address-source.begin+source.offset;
        for (const auto& dest : remote) {
            if (source.path == dest.path && dest.permissions.find('x') != std::string::npos &&
                fileOffset >= dest.offset && fileOffset-dest.offset < dest.end-dest.begin)
                return dest.begin + fileOffset-dest.offset;
        }
    }
    return 0;
}
}
