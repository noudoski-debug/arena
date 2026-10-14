#include "encore/LitvinMenu.h"
#include "Runtime.h"
#include "Menu.h"
#include <cassert>
namespace runtime {
Frame snapshot() { return {{{1,0.5f,0.2f,0.5f,0.8f,12,true,"test"}},1}; }
void setEnabled(bool) {}
const char* status() { return "Host smoke test (no Android / no game)"; }
}
int main() {
    IMGUI_CHECKVERSION(); ImGui::CreateContext();
    auto& io=ImGui::GetIO(); io.IniFilename=nullptr;
    io.DisplaySize=ImVec2(1920,1080); io.DeltaTime=1.f/60.f;
    io.Fonts->AddFontDefault();
    unsigned char* pixels; int w,h; io.Fonts->GetTexDataAsRGBA32(&pixels,&w,&h);
    assert(!LitvinMenu::Supported(LitvinMenu::T("Aim",&Vars.Aimbot.enableAimbot)));
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
