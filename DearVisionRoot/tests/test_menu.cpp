#include "encore/LitvinMenu.h"
#include "Runtime.h"
#include "Menu.h"
#include "Input.h"
#include <cassert>
namespace runtime {
Frame snapshot() { return {{{1,0.5f,0.2f,0.5f,0.8f,12,true,true,1,"test"}},1}; }
void setEnabled(bool) {}
void submit(const AllVars&,modules::Controls) {}
const char* status() { return "Host smoke test (no Android / no game)"; }
}
namespace input {
std::vector<Touch> touches(){return {};}
bool isDown(int){return false;}
bool consumeMenuToggle(){return false;}
}
int main() {
    IMGUI_CHECKVERSION(); ImGui::CreateContext();
    auto& io=ImGui::GetIO(); io.IniFilename=nullptr;
    io.DisplaySize=ImVec2(1920,1080); io.DeltaTime=1.f/60.f;
    io.Fonts->AddFontDefault();
    unsigned char* pixels; int w,h; io.Fonts->GetTexDataAsRGBA32(&pixels,&w,&h);
    assert(LitvinMenu::Supported(LitvinMenu::T("Aim",&Vars.Aimbot.enableAimbot)));
    assert(!LitvinMenu::Supported(LitvinMenu::T("No source",&Vars.Gun.wallshot)));
    assert(LitvinMenu::Supported(LitvinMenu::T("ESP",&Vars.ESP.enable)));
    LitvinMenu::open=true; Vars.ESP.enable=true;
    Vars.ESP.tracer=true; Vars.ESP.outline=true;
    for (int orientation=0;orientation<2;++orientation) {
        io.DisplaySize=orientation?ImVec2(720,1280):ImVec2(1920,1080);
        for (int section=0;section<5;++section) {
            LitvinMenu::g_cur=section;
            for (unsigned tab=0;tab<LitvinMenu::Sections()[section].tabs.size();++tab) {
                LitvinMenu::g_tab[section]=tab;
                ImGui::NewFrame(); menu::render(); ImGui::Render();
                assert(ImGui::GetDrawData()->Valid);
            }
        }
    }
    ImGui::DestroyContext();
}
