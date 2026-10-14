#include "MenuEternalFont.h"
#include "MenuEternalSourceSans.h"
#include "MenuEternalFontAwesome.h"
#include "ImGui/imgui.h"

namespace MenuEternalFont
{
    ImFont* LoadSemiBold(float sizePixels)
    {
        ImFontConfig config{};
        config.FontDataOwnedByAtlas = false;
        return ImGui::GetIO().Fonts->AddFontFromMemoryTTF(
            const_cast<unsigned char*>(kSourceSans3SemiBoldData),
            static_cast<int>(kSourceSans3SemiBoldSize),
            sizePixels,
            &config,
            ImGui::GetIO().Fonts->GetGlyphRangesCyrillic());
    }

    ImFont* LoadIcons(float sizePixels, bool mergeWithLastFont)
    {
        ImFontConfig config{};
        config.FontDataOwnedByAtlas = false;
        config.MergeMode = mergeWithLastFont;
        config.PixelSnapH = true;
        config.GlyphOffset.y = 1.5f;
        static constexpr ImWchar ranges[] = { 0xE000, 0xF8FF, 0 };
        return ImGui::GetIO().Fonts->AddFontFromMemoryTTF(
            const_cast<unsigned char*>(kFontAwesomeSolidData),
            static_cast<int>(kFontAwesomeSolidSize),
            sizePixels,
            &config,
            ranges);
    }
}
