// Vars.cpp — глобалы + конфиг-система
#include "MenuStubs.h"
#include "Android_draw/draw.h"
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <string>
#include <map>
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>

AllVars Vars;

namespace menuscale { float menuscale=1.0f; float menuscalee=1.0f; }
namespace espscale  { float espscale=1.0f; }
namespace tabs      { int cur_tab=0; int page=0; }

void TabSystem::InitializeTabs() {}
void TabSystem::RenderTabs() {
    const char* names[5]={"Combat","Visuals","Misc","FreeCam","Settings"};
    for(int i=0;i<5;i++){
        bool sel=(tabs::cur_tab==i);
        if(sel) ImGui::PushStyleColor(ImGuiCol_Button,ImVec4(0.35f,0.35f,0.35f,1.f));
        if(ImGui::Button(names[i],ImVec2(230,40))) tabs::cur_tab=i;
        if(sel) ImGui::PopStyleColor();
    }
}

namespace c {
ImVec4 accent=ImVec4(0.45f,0.55f,1.0f,1.0f);
namespace elements { ImVec4 text=ImVec4(1,1,1,1); ImVec4 backgroundd=ImVec4(0.08f,0.08f,0.08f,1); }
namespace watermark { ImVec4 outline=ImVec4(0.45f,0.55f,1.0f,1.0f); }
namespace background { ImVec2 size=ImVec2(1020.f,730.f); }
}

ImVec4 opemline      = ImVec4(0.45f,0.55f,1.0f,1.0f);
ImVec4 aimfovcolor   = ImVec4(1,1,1,1);
ImVec4 espboxcolor   = ImVec4(1,1,1,1);
ImVec4 linecolor     = ImVec4(1,1,1,1);
ImVec4 namecolor     = ImVec4(1,1,1,1);
ImVec4 distancecolor = ImVec4(1.0f,0.82f,0.28f,1.0f);
ImVec4 EspHealthGradientColor  = ImVec4(0,1,0,1);
ImVec4 EspHealthGradientColor1 = ImVec4(1,0,0,1);
ImVec4 EspArmorGradientColor   = ImVec4(0,0.5f,1,1);
ImVec4 EspArmorGradientColor1  = ImVec4(0,1,1,1);
ImVec4 EspAmmoGradientColor    = ImVec4(1,1,0,1);
ImVec4 EspAmmoGradientColor1   = ImVec4(1,0.5f,0,1);
ImVec4 EspHealthColor = ImVec4(0,1,0,1);
ImVec4 EspArmorColor  = ImVec4(0,0.5f,1,1);
ImVec4 EspAmmoColor   = ImVec4(1,1,0,1);
ImVec4 sky_color      = ImVec4(0.4f,0.6f,1.0f,1.0f);

namespace edited {
void colortext(ImColor col,const char* text){ ImGui::TextColored(col,"%s",text?text:""); }
bool checkbox(const char* l,bool* v)         { return ImGui::Checkbox(l?l:"##cb",v); }
bool slider_float(const char* l,float* v,float mn,float mx,const char* f)
    { return ImGui::SliderFloat(l?l:"##sl",v,mn,mx,f?f:"%.2f"); }
bool combo(const char* l,int* c,const char*const o[],int n)
    { return ImGui::Combo(l?l:"##co",c,o,n); }
bool buttonn(const char* l,ImVec2 sz){ return ImGui::Button(l?l:"##btn",sz); }
bool color_edit3(const char* l,float* col,int fl){ return ImGui::ColorEdit3(l?l:"##ce",col,fl); }
}

std::vector<std::string> configFiles={"default"};
int selectedConfig=0;
static const char* kCfgDir="/data/local/tmp/67/configs";

static std::string cfgSanitize(const std::string& n){
    std::string o; for(char c:n){
        if((c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9')||c=='_'||c=='-')
            { o.push_back(c); if(o.size()>=32)break; } }
    return o.empty()?std::string("default"):o;
}
static std::string cfgPath(const std::string& n){return std::string(kCfgDir)+"/"+cfgSanitize(n)+".cfg";}
static void cfgPutB(FILE* f,const char* k,bool v)   { fprintf(f,"%s=%d\n",k,v?1:0); }
static void cfgPutI(FILE* f,const char* k,int v)    { fprintf(f,"%s=%d\n",k,v); }
static void cfgPutF(FILE* f,const char* k,float v)  { fprintf(f,"%s=%.4f\n",k,v); }
static void cfgPutC(FILE* f,const char* k,const ImVec4& c)
    { fprintf(f,"%s=%.3f,%.3f,%.3f,%.3f\n",k,c.x,c.y,c.z,c.w); }
static bool cfgGetB(const std::map<std::string,std::string>& m,const char* k,bool& v)
    { auto it=m.find(k);if(it==m.end())return false;v=(atoi(it->second.c_str())!=0);return true; }
static bool cfgGetI(const std::map<std::string,std::string>& m,const char* k,int& v)
    { auto it=m.find(k);if(it==m.end())return false;v=atoi(it->second.c_str());return true; }
static bool cfgGetF(const std::map<std::string,std::string>& m,const char* k,float& v)
    { auto it=m.find(k);if(it==m.end())return false;v=(float)atof(it->second.c_str());return true; }
static bool cfgGetC(const std::map<std::string,std::string>& m,const char* k,ImVec4& c){
    auto it=m.find(k);if(it==m.end())return false;
    float x=c.x,y=c.y,z=c.z,w=c.w;
    if(sscanf(it->second.c_str(),"%f,%f,%f,%f",&x,&y,&z,&w)!=4)return false;
    c=ImVec4(x,y,z,w);return true;
}
static float cfgClampF(float v,float lo,float hi){return(v>=lo&&v<=hi)?v:lo;}
static int   cfgClampI(int v,int lo,int hi){return v<lo?lo:(v>hi?hi:v);}

void RefreshConfigList(){
    configFiles.clear(); mkdir(kCfgDir,0755);
    DIR* d=opendir(kCfgDir); if(d){struct dirent* e;
        while((e=readdir(d))){ std::string n=e->d_name;
            if(n.size()>4&&n.compare(n.size()-4,4,".cfg")==0)
                configFiles.push_back(n.substr(0,n.size()-4)); }
        closedir(d); }
    std::sort(configFiles.begin(),configFiles.end());
    if(configFiles.empty())configFiles.push_back("default");
    selectedConfig=std::max(0,std::min(selectedConfig,(int)configFiles.size()-1));
}
void SaveConfig(const std::string& name){
    std::string sn=cfgSanitize(name); mkdir(kCfgDir,0755);
    FILE* f=fopen(cfgPath(sn).c_str(),"w"); if(!f)return;
    fprintf(f,"# BURDI cfg v2\n");
    // aim
    cfgPutB(f,"aim.enable",Vars.Aimbot.enableAimbot);
    cfgPutB(f,"aim.scope",Vars.Aimbot.scopeCheck);
    cfgPutF(f,"aim.smooth2",Vars.Aimbot.smooth);        // новый smooth 0-100
    cfgPutB(f,"aim.fovc",Vars.Aimbot.fovcheck);
    cfgPutF(f,"aim.fov",Vars.Aimbot.fov);
    cfgPutI(f,"aim.bone",Vars.Aimbot.selectedBoneIndex);
    cfgPutF(f,"aim.maxdist",Vars.Aimbot.maxDistance);
    cfgPutB(f,"aim.showfov",Vars.Aimbot.drawfov);
    cfgPutB(f,"aim.visonly",Vars.Aimbot.visibleOnly);
    cfgPutB(f,"aim.silent",Vars.Aimbot.silentAim);
    cfgPutF(f,"aim.silentfov",Vars.Aimbot.silentFov);
    cfgPutI(f,"aim.silentbone",Vars.Aimbot.silentBoneIndex);
    cfgPutB(f,"aim.silentvis",Vars.Aimbot.silentVisibleOnly);
    cfgPutB(f,"aim.silentscope",Vars.Aimbot.silentScopeCheck);
    // esp
    cfgPutB(f,"esp.enable",Vars.ESP.enable);
    cfgPutB(f,"esp.box",Vars.ESP.box);
    cfgPutB(f,"esp.xray",Vars.ESP.xrayFill);
    cfgPutB(f,"esp.tracer",Vars.ESP.tracer);
    cfgPutB(f,"esp.name",Vars.ESP.name);
    cfgPutB(f,"esp.dist",Vars.ESP.distance);
    cfgPutB(f,"esp.count",Vars.ESP.count);
    cfgPutB(f,"esp.off",Vars.ESP.outOfScreen);
    cfgPutF(f,"esp.arrowr",Vars.ESP.arrowRadius);
    cfgPutF(f,"esp.round",Vars.Settings.espround);
    cfgPutF(f,"esp.camfov",Vars.Settings.camFov);
    // chams
    cfgPutB(f,"chams.enable",Vars.Chams.enable);
    cfgPutB(f,"chams.walls",Vars.Chams.throughWalls);
    cfgPutI(f,"chams.mat",Vars.Chams.material);
    cfgPutF(f,"chams.opacity",Vars.Chams.opacity);
    cfgPutF(f,"chams.r",Vars.Chams.r);
    cfgPutF(f,"chams.g",Vars.Chams.g);
    cfgPutF(f,"chams.b",Vars.Chams.b);
    cfgPutF(f,"chams.wr",Vars.Chams.wr);
    cfgPutF(f,"chams.wg",Vars.Chams.wg);
    cfgPutF(f,"chams.wb",Vars.Chams.wb);
    // misc/player
    cfgPutB(f,"misc.fov",Vars.Player.camerafov);
    cfgPutF(f,"misc.fovv",Vars.Player.customFov);
    cfgPutB(f,"misc.arm",Vars.Player.armfov);
    cfgPutF(f,"misc.armv",Vars.Player.armfovValue);
    cfgPutB(f,"misc.recoil",Vars.Player.norecoil);
    cfgPutB(f,"misc.spread",Vars.Player.nospread);
    // freecam
    cfgPutB(f,"fc.enable",Vars.FreeCam.enable);
    cfgPutF(f,"fc.speed",Vars.FreeCam.speed);
    // menu scale
    cfgPutF(f,"menu.scale",menuscale::menuscalee);
    // colors
    cfgPutC(f,"c.fov",aimfovcolor);
    cfgPutC(f,"c.box",espboxcolor);
    cfgPutC(f,"c.tracer",linecolor);
    cfgPutC(f,"c.name",namecolor);
    cfgPutC(f,"c.distance",distancecolor);
    cfgPutC(f,"c.accent",c::accent);
    fclose(f);
    RefreshConfigList();
    for(int i=0;i<(int)configFiles.size();i++)
        if(configFiles[i]==sn){selectedConfig=i;break;}
}
void LoadConfig(const std::string& name){
    std::string sn=cfgSanitize(name);
    FILE* f=fopen(cfgPath(sn).c_str(),"r"); if(!f)return;
    std::map<std::string,std::string> m;
    char line[256];
    while(fgets(line,sizeof(line),f)){
        if(line[0]=='#'||line[0]=='\n'||line[0]=='\r')continue;
        char* eq=strchr(line,'='); if(!eq)continue;
        *eq='\0'; char* v=eq+1;
        while(*v==' '||*v=='\t')v++;
        char* e=v+strlen(v);
        while(e>v&&(e[-1]=='\n'||e[-1]=='\r'||e[-1]==' '||e[-1]=='\t'))*--e='\0';
        if(*v)m[line]=v;
    }
    fclose(f);
    float fv; int iv; bool bv;
    if(cfgGetB(m,"aim.enable",bv))Vars.Aimbot.enableAimbot=bv;
    if(cfgGetB(m,"aim.scope",bv))Vars.Aimbot.scopeCheck=bv;
    if(cfgGetF(m,"aim.smooth2",fv))Vars.Aimbot.smooth=cfgClampF(fv,0.f,100.f);
    if(cfgGetB(m,"aim.fovc",bv))Vars.Aimbot.fovcheck=bv;
    if(cfgGetF(m,"aim.fov",fv))Vars.Aimbot.fov=cfgClampF(fv,30.f,400.f);
    if(cfgGetI(m,"aim.bone",iv))Vars.Aimbot.selectedBoneIndex=cfgClampI(iv,0,5);
    if(cfgGetF(m,"aim.maxdist",fv))Vars.Aimbot.maxDistance=cfgClampF(fv,50.f,800.f);
    if(cfgGetB(m,"aim.showfov",bv))Vars.Aimbot.drawfov=bv;
    if(cfgGetB(m,"aim.visonly",bv))Vars.Aimbot.visibleOnly=bv;
    if(cfgGetB(m,"aim.silent",bv))Vars.Aimbot.silentAim=bv;
    if(cfgGetF(m,"aim.silentfov",fv))Vars.Aimbot.silentFov=cfgClampF(fv,30.f,400.f);
    if(cfgGetI(m,"aim.silentbone",iv))Vars.Aimbot.silentBoneIndex=cfgClampI(iv,0,5);
    if(cfgGetB(m,"aim.silentvis",bv))Vars.Aimbot.silentVisibleOnly=bv;
    if(cfgGetB(m,"aim.silentscope",bv))Vars.Aimbot.silentScopeCheck=bv;
    if(cfgGetB(m,"esp.enable",bv))Vars.ESP.enable=bv;
    if(cfgGetB(m,"esp.box",bv))Vars.ESP.box=bv;
    if(cfgGetB(m,"esp.xray",bv))Vars.ESP.xrayFill=bv;
    if(cfgGetB(m,"esp.tracer",bv))Vars.ESP.tracer=bv;
    if(cfgGetB(m,"esp.name",bv))Vars.ESP.name=bv;
    if(cfgGetB(m,"esp.dist",bv))Vars.ESP.distance=bv;
    if(cfgGetB(m,"esp.count",bv))Vars.ESP.count=bv;
    if(cfgGetB(m,"esp.off",bv))Vars.ESP.outOfScreen=bv;
    if(cfgGetF(m,"esp.arrowr",fv))Vars.ESP.arrowRadius=cfgClampF(fv,60.f,400.f);
    if(cfgGetF(m,"esp.round",fv))Vars.Settings.espround=cfgClampF(fv,0.f,16.f);
    if(cfgGetF(m,"esp.camfov",fv))Vars.Settings.camFov=cfgClampF(fv,30.f,100.f);
    if(cfgGetB(m,"chams.enable",bv))Vars.Chams.enable=bv;
    if(cfgGetB(m,"chams.walls",bv))Vars.Chams.throughWalls=bv;
    if(cfgGetI(m,"chams.mat",iv))Vars.Chams.material=cfgClampI(iv,0,2);
    if(cfgGetF(m,"chams.opacity",fv))Vars.Chams.opacity=cfgClampF(fv,0.f,1.f);
    if(cfgGetF(m,"chams.r",fv))Vars.Chams.r=cfgClampF(fv,0.f,1.f);
    if(cfgGetF(m,"chams.g",fv))Vars.Chams.g=cfgClampF(fv,0.f,1.f);
    if(cfgGetF(m,"chams.b",fv))Vars.Chams.b=cfgClampF(fv,0.f,1.f);
    if(cfgGetF(m,"chams.wr",fv))Vars.Chams.wr=cfgClampF(fv,0.f,1.f);
    if(cfgGetF(m,"chams.wg",fv))Vars.Chams.wg=cfgClampF(fv,0.f,1.f);
    if(cfgGetF(m,"chams.wb",fv))Vars.Chams.wb=cfgClampF(fv,0.f,1.f);
    if(cfgGetB(m,"misc.fov",bv))Vars.Player.camerafov=bv;
    if(cfgGetF(m,"misc.fovv",fv))Vars.Player.customFov=cfgClampF(fv,40.f,120.f);
    if(cfgGetB(m,"misc.arm",bv))Vars.Player.armfov=bv;
    if(cfgGetF(m,"misc.armv",fv))Vars.Player.armfovValue=cfgClampF(fv,20.f,120.f);
    if(cfgGetB(m,"misc.recoil",bv))Vars.Player.norecoil=bv;
    if(cfgGetB(m,"misc.spread",bv))Vars.Player.nospread=bv;
    if(cfgGetB(m,"fc.enable",bv))Vars.FreeCam.enable=bv;
    if(cfgGetF(m,"fc.speed",fv))Vars.FreeCam.speed=cfgClampF(fv,1.f,50.f);
    if(cfgGetF(m,"menu.scale",fv)){menuscale::menuscalee=cfgClampF(fv,0.5f,1.5f);menuscale::menuscale=menuscale::menuscalee;}
    ImVec4 cc;
    cc=aimfovcolor;  if(cfgGetC(m,"c.fov",cc))aimfovcolor=cc;
    cc=espboxcolor;  if(cfgGetC(m,"c.box",cc))espboxcolor=cc;
    cc=linecolor;    if(cfgGetC(m,"c.tracer",cc))linecolor=cc;
    cc=namecolor;    if(cfgGetC(m,"c.name",cc))namecolor=cc;
    cc=distancecolor;if(cfgGetC(m,"c.distance",cc))distancecolor=cc;
    cc=c::accent;    if(cfgGetC(m,"c.accent",cc))c::accent=cc;
}
void DeleteConfig(){
    if(selectedConfig<0||selectedConfig>=(int)configFiles.size())return;
    std::string sn=configFiles[selectedConfig];
    unlink(cfgPath(sn).c_str());
    RefreshConfigList(); selectedConfig=0;
}
std::string GetNextAvailableConfigName(){
    RefreshConfigList(); int i=0;
    while(true){ std::string n="config"+std::to_string(i);
        if(std::find(configFiles.begin(),configFiles.end(),n)==configFiles.end())return n; i++; }
}

namespace font {
ImFont* icomoon=nullptr; ImFont* lexend_bold=nullptr; ImFont* lexend_regular=nullptr;
ImFont* lexend_general_bold=nullptr; ImFont* icomoon_widget=nullptr; ImFont* icomoon_widget2=nullptr;
}

int abs_ScreenX=0, abs_ScreenY=0;
