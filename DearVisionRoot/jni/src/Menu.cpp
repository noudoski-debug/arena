#include "encore/LitvinMenu.h"
#include "Runtime.h"
#include "Menu.h"
#include "Target.h"
#include "Input.h"
#include "Chams.h"
#include "Config.h"
#include "Effects.h"
#include <algorithm>

AllVars Vars;
namespace menu {
modules::Vec2 joystick(const std::vector<input::Touch>& touches,ImVec2 center,float radius,int& id) {
    ImDrawList* d=ImGui::GetBackgroundDrawList();
    modules::Vec2 result;
    bool active=false;
    for(const auto& t:touches) {
        float dx=t.x-center.x,dy=t.y-center.y;
        if(t.id==id || (id<0 && std::hypot(dx,dy)<radius)) {
            id=t.id;float length=std::max(radius,std::hypot(dx,dy));
            result={dx/length,dy/length};active=true;break;
        }
    }
    if(!active) id=-1;
    d->AddCircleFilled(center,radius,IM_COL32(30,35,50,170),40);
    d->AddCircle(center,radius,IM_COL32(90,165,250,230),40,2.f);
    d->AddCircleFilled(ImVec2(center.x+result.x*radius*.6f,center.y+result.y*radius*.6f),radius*.3f,IM_COL32(100,180,255,240));
    return result;
}
void pause() {
    Vars.ESP.enable=false;Vars.Chams.enable=false;Vars.Aimbot.enableAimbot=false;Vars.Aimbot.silentAim=false;
    Vars.FreeCam.enable=false;Vars.Player.camerafov=false;Vars.Player.armfov=false;
    Vars.Player.aspect_ratio=false;Vars.Player.norecoil=false;Vars.Player.nospread=false;
}
void profiles(float width,float height) {
    ImGui::SetNextWindowSize(ImVec2(std::min(width-24.f,540.f),0),ImGuiCond_Appearing);
    if(!ImGui::BeginPopupModal("Profiles and effect preview",nullptr,ImGuiWindowFlags_AlwaysAutoResize))return;
    static std::string current="default";
    auto names=config::list();
    if(ImGui::BeginCombo("Profile",current.c_str())) {
        for(const auto& name:names)if(ImGui::Selectable(name.c_str(),name==current))current=name;
        ImGui::EndCombo();
    }
    if(ImGui::Button("New profile")) {
        int n=1;do { current="profile"+std::to_string(n++); }while(std::find(names.begin(),names.end(),current)!=names.end());
        config::save(current,Vars);
    }
    ImGui::SameLine();if(ImGui::Button("Save"))config::save(current,Vars);
    ImGui::SameLine();if(ImGui::Button("Load"))config::load(current,Vars);
    ImGui::SameLine();if(ImGui::Button("Delete"))config::remove(current);
    ImGui::TextWrapped("%s",config::status().c_str());
    ImGui::Separator();
    ImGui::TextWrapped("HitFX renderer ported. No automatic hit/death event source was provided in Encore. These buttons are previews only.");
    ImGui::Checkbox("Effects",&HitEffect::enabled);
    ImGui::Checkbox("Hit effects",&HitEffect::hitEnabled);ImGui::SameLine();ImGui::Checkbox("Kill effects",&HitEffect::killEnabled);
    if(ImGui::Button("Preview hit"))effects::push({width*.5f,height*.3f,25,false});
    ImGui::SameLine();if(ImGui::Button("Preview kill"))effects::push({width*.5f,height*.3f,0,true});
    if(ImGui::Button("Close"))ImGui::CloseCurrentPopup();
    ImGui::EndPopup();
}
void render() {
    ImGuiIO& io = ImGui::GetIO();
    const float width = io.DisplaySize.x, height = io.DisplaySize.y;
    if(input::consumeMenuToggle())LitvinMenu::open=!LitvinMenu::open;
    const runtime::Frame frame = runtime::snapshot();
    Vars.FreeCam.found=frame.freecam;
    for(const auto& e:effects::consume()) {
        if(e.kill)HitEffect::onKill(e.x,e.y);else HitEffect::onHit(e.x,e.y,e.damage);
    }
    std::vector<chams::ChamsPlayer> silhouettes;
    ImDrawList* dl = ImGui::GetBackgroundDrawList();
    if (Vars.ESP.enable || Vars.Chams.enable) {
        if (Vars.ESP.enable && Vars.ESP.count) {
            char text[64]; std::snprintf(text, sizeof(text), "Players tracked: %u", frame.players);
            dl->AddText(ImVec2(width * 0.5f - 80.f, 18.f), IM_COL32_WHITE, text);
        }
        for (const auto& box : frame.boxes) {
            if(Vars.ESP.enable && Vars.ESP.outOfScreen && !box.onScreen) {
                float dx=box.headX-.5f,dy=box.headY-.5f;
                if(box.depth<0){dx=-dx;dy=-dy;}
                float len=std::hypot(dx,dy);
                if(std::isfinite(len) && len>.001f) {
                    dx/=len;dy/=len;
                    float radius=std::min(Vars.ESP.arrowRadius,std::min(width,height)*.42f);
                    ImVec2 p(width*.5f+dx*radius,height*.5f+dy*radius);
                    dl->AddTriangleFilled(ImVec2(p.x+dx*12,p.y+dy*12),
                        ImVec2(p.x-dx*8-dy*7,p.y-dy*8+dx*7),
                        ImVec2(p.x-dx*8+dy*7,p.y-dy*8-dx*7),IM_COL32(190,220,255,235));
                }
            }
            if (!box.projected) continue;
            float top = box.headY*height, bottom = box.feetY*height;
            const float boxHeight = bottom-top;
            if (!std::isfinite(boxHeight) || boxHeight < 2.f || boxHeight > height*4.f) continue;
            float halfWidth = boxHeight * 0.325f, center = box.headX*width;
            if (center+halfWidth < 0 || center-halfWidth > width || bottom < 0 || top > height) continue;
            const ImVec2 lo(center-halfWidth, top), hi(center+halfWidth, bottom);
            const ImU32 color = ImGui::ColorConvertFloat4ToU32(ImVec4(Vars.ESP.boxColor[0],Vars.ESP.boxColor[1],Vars.ESP.boxColor[2],1));
            silhouettes.push_back({center,bottom,halfWidth*2,boxHeight,box.onScreen});
            if(!Vars.ESP.enable)continue;
            if (Vars.ESP.box) {
                if (Vars.ESP.xrayFill) dl->AddRectFilled(lo, hi, (color & 0x00FFFFFF) | (32u<<24), Vars.Settings.espround);
                if (Vars.ESP.outline) dl->AddRect(lo, hi, IM_COL32(0,0,0,220), Vars.Settings.espround, 0, 3.5f);
                dl->AddRect(lo, hi, color, Vars.Settings.espround, 0, 1.5f);
            }
            if (Vars.ESP.tracer) {
                const auto* c = Vars.ESP.lineColor;
                dl->AddLine(ImVec2(width*0.5f, 0), ImVec2(center, top),
                            ImGui::ColorConvertFloat4ToU32(ImVec4(c[0],c[1],c[2],1)), Vars.ESP.lineThickness);
            }
            std::string label = Vars.ESP.name ? box.label : "";
            if (Vars.ESP.distance && std::isfinite(box.distance)) {
                char distance[32]; std::snprintf(distance, sizeof(distance), " %.0fm", box.distance);
                label += distance;
            }
            if (!label.empty()) dl->AddText(ImVec2(center-ImGui::CalcTextSize(label.c_str()).x*0.5f, top-ImGui::GetFontSize()-2.f), color, label.c_str());
        }
    }
    chams::draw(dl,silhouettes.data(),static_cast<int>(silhouettes.size()));
    if(Vars.Aimbot.enableAimbot && Vars.Aimbot.drawfov)
        dl->AddCircle(ImVec2(width*.5f,height*.5f),Vars.Aimbot.fov,IM_COL32(255,255,255,180),64,1.5f);
    if(Vars.Aimbot.silentAim) {
        for(int i=0;i<48;i+=2) {
            float a=i*6.2831853f/48,b=(i+1)*6.2831853f/48,r=Vars.Aimbot.silentFov;
            dl->AddLine(ImVec2(width*.5f+std::cos(a)*r,height*.5f+std::sin(a)*r),
                        ImVec2(width*.5f+std::cos(b)*r,height*.5f+std::sin(b)*r),IM_COL32(255,190,60,200),1.5f);
        }
    }
    modules::Controls controls;
    controls.aimHeld=input::isDown(Vars.Aimbot.aimBind);
    if(Vars.FreeCam.enable && frame.freecam && !LitvinMenu::open) {
        auto touches=input::touches();
        static int moveId=-1,lookId=-1;
        const float radius=std::min(60.f,height*.15f),y=height-radius-35;
        auto move=joystick(touches,ImVec2(width*.2f,y),radius,moveId);
        auto look=joystick(touches,ImVec2(width*.8f,y),radius,lookId);
        controls.moveX=move.x;controls.moveY=move.y;controls.lookX=look.x;controls.lookY=look.y;
        for(int direction:{-1,1}) {
            ImVec2 center(width*.5f+direction*45.f,y);
            dl->AddCircleFilled(center,30,IM_COL32(45,65,100,190));
            dl->AddText(ImVec2(center.x-9,center.y-12),IM_COL32_WHITE,direction>0?"UP":"DN");
            for(const auto& t:touches)if(std::hypot(t.x-center.x,t.y-center.y)<30)controls.vertical+=direction;
        }
    }
    ImGui::SetNextWindowPos(ImVec2(12, 12), ImGuiCond_Always);
    ImGui::SetNextWindowBgAlpha(0.9f);
    ImGui::Begin("DearVision status", nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings);
    if (ImGui::Button("DearVision / INTERNAL ROOT", ImVec2(320, 42))) LitvinMenu::open = !LitvinMenu::open;
    ImGui::Text("Build: %s", target::Version);
    ImGui::TextUnformatted(runtime::status());
    if(LitvinMenu::open && ImGui::Button("Profiles / effect preview"))ImGui::OpenPopup("Profiles and effect preview");
    profiles(width,height);
    ImGui::End();
    LitvinMenu::Render();
    if (LitvinMenu::ShouldExit()) pause(); // NEVER exit() the host game.
    Vars.Menu.openmenu=LitvinMenu::open;
    runtime::submit(Vars,controls);
}
}
