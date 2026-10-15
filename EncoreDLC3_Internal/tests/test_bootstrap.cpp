#define ENCORE_DIAGNOSTICS_TEST
#include "Hooks.h"
#include <atomic>
#include <cassert>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <thread>
#include <vector>
namespace {
std::atomic<int> inHook{0},maximum{0};
std::string contents(const std::filesystem::path& path) {
    std::ifstream input(path);return {std::istreambuf_iterator<char>(input),std::istreambuf_iterator<char>()};
}
}
extern "C" int DobbyHook(void* address,void*,void** original) {
    const int active=++inHook;
    int old=maximum.load();while(old<active && !maximum.compare_exchange_weak(old,active)){}
    for(int i=0;i<1000;++i)std::this_thread::yield();
    if(original)*original=address;
    --inHook;return 0;
}
int main() {
    char name[]="/tmp/encore-bootstrap-XXXXXX";
    assert(mkdtemp(name));const std::filesystem::path directory=name;
    const auto file=directory/"startup.log";
    diagnostics::testFile(file.c_str());
    errno=EACCES;diagnostics::log("first marker %d",42);assert(errno==EACCES);
    assert(contents(file).find("first marker 42")!=std::string::npos);
    std::vector<std::thread> writers;
    for(int i=0;i<4;++i)writers.emplace_back([i] {
        for(int j=0;j<50;++j)diagnostics::log("writer=%d marker=%d",i,j);
    });
    for(auto& thread:writers)thread.join();
    auto text=contents(file);
    for(int i=0;i<4;++i)for(int j=0;j<50;++j)
        assert(text.find("writer="+std::to_string(i)+" marker="+std::to_string(j)+"\n")!=std::string::npos);
    struct stat info{};assert(stat(file.c_str(),&info)==0 && (info.st_mode&0777)==0600);
    // App-controlled symlinks are not followed by the diagnostic writer.
    const auto link=directory/"symlink.log";
    std::filesystem::create_symlink(file,link);diagnostics::testFile(link.c_str());
    diagnostics::log("must not follow symlink");assert(contents(file)==text);
    diagnostics::testFile(file.c_str());
    int first=1,second=2;void* a=nullptr;void* b=nullptr;
    std::thread one([&]{assert(hooks::install(&first,&first,&a,"first")==0);});
    std::thread two([&]{assert(hooks::install(&second,&second,&b,"second")==0);});
    one.join();two.join();assert(a==&first && b==&second && maximum==1);
    text=contents(file);
    assert(text.find("HOOK first end rc=0")!=std::string::npos);
    assert(text.find("HOOK second end rc=0")!=std::string::npos);
    diagnostics::testFile("");std::filesystem::remove_all(directory);
}
