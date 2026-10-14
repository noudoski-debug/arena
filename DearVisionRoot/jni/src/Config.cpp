#include "Config.h"
#include <dirent.h>
#include <sys/stat.h>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <cmath>
#include <map>
#include <type_traits>
#include <algorithm>
#include <cerrno>
#include <cstdlib>
#include <cstdio>
#include <unistd.h>
namespace config {
namespace {
struct State {
    std::string directory="/data/user/0/com.catsbit.oxidesurvivalisland/files/dearvision/configs";
    std::string message="Profiles are stored in the game's private files directory";
};
State& state(){static State value;return value;}
std::string& directory(){return state().directory;}
std::string& message(){return state().message;}
bool validName(const std::string& s) {
    return !s.empty() && s.size()<=32 && std::all_of(s.begin(),s.end(),[](unsigned char c) {
        return (c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9')||c=='_'||c=='-';
    });
}
// One schema shared by reader/writer: disabled source-less controls are never imported.
template<class F> void schema(AllVars& v,F f) {
#define B(key,field) f(key,v.field,0,1)
#define N(key,field,lo,hi) f(key,v.field,lo,hi)
    B("esp.enable",ESP.enable);B("esp.box",ESP.box);B("esp.id",ESP.name);
    B("esp.distance",ESP.distance);B("esp.lines",ESP.tracer);B("esp.fill",ESP.xrayFill);
    B("esp.count",ESP.count);B("esp.arrows",ESP.outOfScreen);B("esp.outline",ESP.outline);
    N("esp.round",Settings.espround,0,16);N("esp.lineWidth",ESP.lineThickness,1,5);
    N("esp.arrowRadius",ESP.arrowRadius,40,400);
    N("box.r",ESP.boxColor[0],0,1);N("box.g",ESP.boxColor[1],0,1);N("box.b",ESP.boxColor[2],0,1);
    N("line.r",ESP.lineColor[0],0,1);N("line.g",ESP.lineColor[1],0,1);N("line.b",ESP.lineColor[2],0,1);
    B("aim.enable",Aimbot.enableAimbot);B("aim.scope",Aimbot.scopeCheck);B("aim.screen",Aimbot.visibleOnly);
    B("aim.visibleCheck",Aimbot.visibleCheck);
    B("aim.draw",Aimbot.drawfov);B("aim.fovCheck",Aimbot.fovcheck);N("aim.fov",Aimbot.fov,30,400);
    N("aim.smooth",Aimbot.smooth,0,100);N("aim.distance",Aimbot.maxDistance,50,800);
    N("aim.bone",Aimbot.selectedBoneIndex,0,5);N("aim.bind",Aimbot.aimBind,0,767);
    B("origin.enable",Aimbot.silentAim);B("origin.scope",Aimbot.silentScopeCheck);
    B("origin.screen",Aimbot.silentVisibleOnly);N("origin.fov",Aimbot.silentFov,30,400);
    N("origin.bone",Aimbot.silentBoneIndex,0,5);
    B("weapon.recoil",Player.norecoil);B("weapon.spread",Player.nospread);
    B("camera.fov",Player.camerafov);N("camera.fovValue",Player.customFov,40,120);
    B("camera.arms",Player.armfov);N("camera.armsValue",Player.armfovValue,20,120);
    B("camera.aspect",Player.aspect_ratio);N("camera.aspectValue",Player.aspect_value,.5,2.5);
    B("freecam.enable",FreeCam.enable);N("freecam.speed",FreeCam.speed,1,50);
    B("chams.enable",Chams.enable);B("chams.filtered",Chams.throughWalls);
    N("chams.style",Chams.material,0,2);N("chams.opacity",Chams.opacity,.05,1);
    N("chams.r",Chams.r,0,1);N("chams.g",Chams.g,0,1);N("chams.b",Chams.b,0,1);
    N("chams.wr",Chams.wr,0,1);N("chams.wg",Chams.wg,0,1);N("chams.wb",Chams.wb,0,1);
    N("menu.theme",Settings.theme,0,3);
#undef B
#undef N
}
std::string join(const std::string& base,const std::string& name) {
    return base+(base.empty() || base.back()=='/'?"":"/")+name;
}
// Use Android/POSIX APIs: older AIDE libc++ distributions lack <filesystem>.
bool ensureDirectory(const std::string& path) {
    if (::mkdir(path.c_str(),0700)==0) return true;
    if (errno==EEXIST) {
        struct stat info{};
        if (::stat(path.c_str(),&info)==0) {
            if (S_ISDIR(info.st_mode)) return true;
            errno=ENOTDIR;
        }
    }
    message()="Cannot create profile directory: "+std::string(std::strerror(errno));
    return false;
}
bool createDirectories(const std::string& path) {
    if (path.empty()) { message()="Profile directory is empty";return false; }
    for (size_t i=1;i<=path.size();++i) {
        if ((i==path.size() || path[i]=='/') && !ensureDirectory(path.substr(0,i))) return false;
    }
    return true;
}
bool path(const std::string& name,std::string& out) {
    if (!validName(name)) { message()="Invalid profile name";return false; }
    if (directory().empty()) { message()="Profile directory is empty";return false; }
    out=join(directory(),name+".cfg");return true;
}
}
#ifdef DEAR_HOST_TEST
void setDirectory(const std::string& value) { directory()=value; }
#endif
const std::string& status() { return message(); }
bool save(const std::string& name,const AllVars& vars) {
    std::string file;if(!path(name,file))return false;
    if(!createDirectories(directory()))return false;
    std::string tmp=file+".tmp";
    std::ofstream stream(tmp,std::ios::trunc);
    if(!stream){message()="Cannot write profile";return false;}
    stream<<"# DearVision internal profile v1\n"<<std::setprecision(9);
    AllVars copy=vars;
    schema(copy,[&](const char* key,auto& v,double,double){stream<<key<<'='<<v<<'\n';});
    stream.close();
    if(!stream || std::rename(tmp.c_str(),file.c_str())!=0){std::remove(tmp.c_str());message()="Profile save failed";return false;}
    message()="Saved "+name;return true;
}
bool load(const std::string& name,AllVars& vars) {
    std::string file;if(!path(name,file))return false;
    std::ifstream stream(file);if(!stream){message()="Cannot read profile";return false;}
    std::map<std::string,std::string> values;
    std::string line;
    while(std::getline(stream,line)){
        if(line.size()>512 || values.size()>128){message()="Profile exceeds limits";return false;}
        if(line.empty() || line[0]=='#')continue;
        size_t eq=line.find('=');if(eq==std::string::npos){message()="Malformed profile";return false;}
        auto key=line.substr(0,eq);
        if(!values.emplace(key,line.substr(eq+1)).second){message()="Duplicate profile field";return false;}
    }
    if(!stream.eof()){message()="Profile read error";return false;}
    AllVars candidate=vars;bool ok=true;
    schema(candidate,[&](const char* key,auto& value,double lo,double hi){
        auto it=values.find(key);if(it==values.end())return;
        char* end=nullptr;errno=0;double parsed=std::strtod(it->second.c_str(),&end);
        using T=std::decay_t<decltype(value)>;
        if(errno || end==it->second.c_str() || *end || !std::isfinite(parsed) || parsed<lo || parsed>hi){ok=false;return;}
        if constexpr(std::is_integral_v<T>) {if(std::floor(parsed)!=parsed){ok=false;return;}}
        value=static_cast<T>(parsed);
    });
    if(!ok){message()="Invalid profile values; nothing applied";return false;}
    vars=candidate;message()="Loaded "+name;return true;
}
bool remove(const std::string& name) {
    std::string file;if(!path(name,file))return false;
    if(std::remove(file.c_str())!=0){message()="Cannot delete profile";return false;}
    message()="Deleted "+name;return true;
}
std::vector<std::string> list() {
    std::vector<std::string> result;
    DIR* dir=::opendir(directory().c_str());
    if (!dir) return result;
    while (const auto* entry=::readdir(dir)) {
        const std::string name=entry->d_name;
        if (name.size()<=4 || name.compare(name.size()-4,4,".cfg")!=0) continue;
        const std::string stem=name.substr(0,name.size()-4);
        if (!validName(stem)) continue;
        struct stat info{};
        const std::string file=join(directory(),name);
        if (::stat(file.c_str(),&info)==0 && S_ISREG(info.st_mode)) result.push_back(stem);
    }
    ::closedir(dir);
    std::sort(result.begin(),result.end());return result;
}
}
