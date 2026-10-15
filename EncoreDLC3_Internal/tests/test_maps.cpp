#include "../jni/loader/Maps.h"
#include <cassert>
int main() {
    loader::Mapping m;
    assert(loader::parseMapping("1000-2000 r-xp 00004000 08:01 123 /path/with space.so\n", m));
    assert(m.begin==0x1000 && m.end==0x2000 && m.offset==0x4000 && m.path=="/path/with space.so");
    assert(!loader::parseMapping("broken",m));
    assert(!loader::parseMapping("2000-1000 r-xp 0 00:00 0",m));
    std::vector<loader::Mapping> local{{0x1000,0x2000,0x4000,"r-xp","/lib.so"}};
    std::vector<loader::Mapping> remote{{0xA000,0xB000,0x4000,"r-xp","/lib.so"}};
    assert(loader::relocate(local,remote,0x1500)==0xA500);
    assert(loader::relocate(local,remote,0x2000)==0);
    remote[0].permissions="r--p"; assert(loader::relocate(local,remote,0x1500)==0);
    remote[0].permissions="r-xp"; remote[0].path="/different/lib.so";
    assert(loader::relocate(local,remote,0x1500)==0);
}
