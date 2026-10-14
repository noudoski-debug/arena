#pragma once
// MenuStubs.h — заглушки зависимостей menu.hpp (только папка 67).
// Даёт: edited::виджеты, tabs, TabSystem, c::цвета, menuscale/espscale,
// глобальные цвета, конфиг-систему, алиасы недостающих шрифтов.
#include "imgui.h"
#include "Vars.h"
#include <vector>
#include <string>

// ---------- scale ----------
namespace menuscale {
extern float menuscale;
extern float menuscalee;
}
namespace espscale {
extern float espscale;
}

// ---------- tabs ----------
namespace tabs {
extern int cur_tab;
extern int page;
}

struct TabSystem {
    void InitializeTabs();
    void RenderTabs();
};

// ---------- colors / theme ----------
namespace c {
extern ImVec4 accent;
namespace elements {
extern ImVec4 text;
extern ImVec4 backgroundd;
}
namespace watermark {
extern ImVec4 outline;
}
namespace background {
extern ImVec2 size;
}
}

// Глобальные цвета из menu.hpp (ImVec4, правятся через color_edit3)
extern ImVec4 opemline;
extern ImVec4 aimfovcolor;
extern ImVec4 espboxcolor;
extern ImVec4 linecolor;
extern ImVec4 namecolor;
extern ImVec4 distancecolor;
extern ImVec4 EspHealthGradientColor;
extern ImVec4 EspHealthGradientColor1;
extern ImVec4 EspArmorGradientColor;
extern ImVec4 EspArmorGradientColor1;
extern ImVec4 EspAmmoGradientColor;
extern ImVec4 EspAmmoGradientColor1;
extern ImVec4 EspHealthColor;
extern ImVec4 EspArmorColor;
extern ImVec4 EspAmmoColor;
extern ImVec4 sky_color;

// ---------- edited widgets (тонкие обёртки над ImGui) ----------
namespace edited {
void colortext(ImColor col, const char* text);
bool checkbox(const char* label, bool* v);
bool slider_float(const char* label, float* v, float v_min, float v_max, const char* fmt = "%.2f");
bool combo(const char* label, int* cur, const char* const items[], int items_count);
bool buttonn(const char* label, ImVec2 size);
bool color_edit3(const char* label, float* col, int flags = 0);
} // namespace edited

// ---------- font:: указатели темы (дефайн в Vars.cpp) ----------
namespace font {
extern ImFont* icomoon;
extern ImFont* lexend_bold;
extern ImFont* lexend_regular;
extern ImFont* lexend_general_bold;
extern ImFont* icomoon_widget;
extern ImFont* icomoon_widget2;
}

// ---------- config stubs ----------
extern std::vector<std::string> configFiles;
extern int selectedConfig;
void RefreshConfigList();
void LoadConfig(const std::string& name);
void SaveConfig(const std::string& name);
void DeleteConfig();
std::string GetNextAvailableConfigName();

// ---------- FontAwesome range fallback (нет fa-хидера в 67) ----------
#ifndef ICON_MIN_FA
#define ICON_MIN_FA 0xE000
#define ICON_MAX_FA 0xF000
#endif
// NOTE: недостающие бинари шрифтов (lixendfont, icomoon_widget-bin,
// font_awesome_binary, omnis_, montserrat) в draw.cpp заменены на
// существующие Verdana[]/regular[] (см. патч draw.cpp), отдельных
// определений не требуется.
