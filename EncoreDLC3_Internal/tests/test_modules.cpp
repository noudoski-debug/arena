#include "ModuleMath.h"
#include "Config.h"
#include "Effects.h"
#include <cassert>
#include <filesystem>
#include <fstream>
#include <limits>
#include <unistd.h>
int main() {
    auto aimed=modules::aimStep({350,0},{10,90},0,1.f/60.f);
    assert(aimed.x==370 && aimed.y==85);
    auto still=modules::aimStep({0,0},{90,90},0,0);
    assert(still.x==0 && still.y==0);
    auto slow=modules::aimStep({0,0},{90,0},100,1.f/60.f);
    auto fast=modules::aimStep({0,0},{90,0},0,1.f/60.f);
    assert(fast.x>slow.x);
    // Same convergence at 30, 60 and 120 Hz; wrap must always take the short arc.
    float reference=0;
    for(int hz:{30,60,120}) {
        modules::Vec2 current{350,0};
        for(int i=0;i<hz;++i)current=modules::aimStep(current,{10,30},45,1.f/hz);
        if(!reference)reference=current.x;
        assert(std::fabs(current.x-reference)<.001f);
        assert(current.x>350 && current.x<=370);
    }
    auto invalid=modules::aimStep({1,2},{std::numeric_limits<float>::quiet_NaN(),0},45,.016f);
    assert(invalid.x==1 && invalid.y==2);
    auto head=modules::bone({0,0,0},{0,2,0},{1,0,0},0);
    auto arm=modules::bone({0,0,0},{0,2,0},{1,0,0},4);
    assert(head.y==2 && arm.x>0 && arm.y<head.y);
    int value=10;
    try { modules::ScopedValue<int> patch(&value,20,true); assert(value==20); throw 1; }catch(int){}
    assert(value==10);
    {modules::ScopedValue<int> patch(nullptr,5,false);}
    auto dir=std::filesystem::temp_directory_path()/ ("dear-profile-test-"+std::to_string(getpid()));
    config::setDirectory(dir.string());
    AllVars vars;vars.Aimbot.enableAimbot=true;vars.Aimbot.deathCheck=false;vars.Aimbot.silentDeathCheck=false;vars.Aimbot.visibleOnly=true;vars.Aimbot.silentAim=true;vars.FreeCam.speed=12;vars.ESP.boxColor[0]=.25f;
    assert(config::save("profile1",vars));
    AllVars restored;assert(config::load("profile1",restored));
    assert(restored.Aimbot.enableAimbot && restored.FreeCam.speed==12 && restored.ESP.boxColor[0]==.25f);
    assert(!restored.Aimbot.deathCheck && !restored.Aimbot.silentDeathCheck && restored.Aimbot.visibleOnly && restored.Aimbot.silentAim);
    assert(config::list().size()==1);
    for(const char* invalid:{"aim.enable=0\nfreecam.speed=nan\n","aim.bone=2.5\n","aim.fov=-1\n","esp.enable=2\n","aim.fov=40\naim.fov=50\n"}) {
        std::ofstream(dir/"bad.cfg")<<invalid;
        assert(!config::load("bad",restored));
        assert(restored.Aimbot.enableAimbot); // Transactional: no half-applied changes.
    }
    assert(!config::save("../escape",vars));
    assert(!config::load("/absolute",vars));
    assert(config::remove("profile1"));
    std::filesystem::remove_all(dir);
    EncoreNotifyHit(10,20,30,0);
    assert(effects::consume().size()==1 && effects::consume().empty());
    EncoreNotifyHit(std::numeric_limits<float>::quiet_NaN(),0,1,0);
    assert(effects::consume().empty());
}
