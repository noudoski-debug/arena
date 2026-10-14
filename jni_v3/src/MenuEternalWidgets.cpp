#include "MenuEternalWidgets.h"

#include <cstdio>
#include <cmath>
#include <cstddef>

#include "MenuEternalTheme.h"

namespace MenuEternal::Widgets
{
    bool Panel(ImDrawList* drawList, const char* id, ImVec2 pos, ImVec2 size, const char* title, bool* expanded)
    {
        drawList->AddRectFilled(pos, ImVec2(pos.x + size.x, pos.y + size.y), Theme::Surface(246), 4.0f);
        drawList->AddRect(pos, ImVec2(pos.x + size.x, pos.y + size.y), Theme::Outline(180), 4.0f);
        drawList->AddLine(ImVec2(pos.x, pos.y + 30.0f), ImVec2(pos.x + size.x, pos.y + 30.0f), Theme::Outline(145), 1.0f);
        drawList->AddText(ImVec2(pos.x + 12.0f, pos.y + 8.0f), Theme::Text(), title);

        ImGui::PushID(id);
        ImVec2 buttonPos(pos.x + size.x - 54.0f, pos.y + 3.0f);
        ImGui::SetCursorScreenPos(buttonPos);
        // Keep the archive's small arrow, but give it a phone-sized hit area.
        ImGui::InvisibleButton("##panel_toggle", ImVec2(46.0f, 30.0f));
        bool hovered = ImGui::IsItemHovered();
        if (ImGui::IsItemClicked())
        {
            *expanded = !*expanded;
        }
        ImGui::PopID();

        ImU32 arrowColor = hovered ? Theme::Accent() : Theme::Text();
        drawList->AddText(ImVec2(pos.x + size.x - 22.0f, pos.y + 8.0f), arrowColor, *expanded ? u8"\uf0d7" : u8"\uf0da");
        return *expanded;
    }

    void Checkbox(ImDrawList* drawList, const char* id, ImVec2 pos, const char* label, bool* value)
    {
        const float boxSize = 32.0f;
        const float hitHeight = 36.0f;
        ImVec2 textSize = ImGui::CalcTextSize(label);
        ImGui::PushID(id);
        ImGui::SetCursorScreenPos(pos);
        ImGui::InvisibleButton("##checkbox", ImVec2(boxSize + 12.0f + textSize.x, hitHeight));
        bool hovered = ImGui::IsItemHovered();
        if (ImGui::IsItemClicked())
        {
            *value = !*value;
        }
        ImGui::PopID();

        ImU32 boxColor = *value ? Theme::Accent(230) : Theme::Control();
        ImU32 outlineColor = *value ? Theme::AccentSoft(180) : hovered ? Theme::Accent(115) : Theme::Outline();
        ImU32 labelColor = (*value || hovered) ? Theme::Text() : Theme::MutedText();
        const float boxY = pos.y + (hitHeight - boxSize) * 0.5f;
        drawList->AddRectFilled(ImVec2(pos.x + 0.5f, boxY + 0.5f), ImVec2(pos.x + boxSize - 0.5f, boxY + boxSize - 0.5f), boxColor, 5.0f);
        drawList->AddRect(ImVec2(pos.x + 0.5f, boxY + 0.5f), ImVec2(pos.x + boxSize - 0.5f, boxY + boxSize - 0.5f), outlineColor, 5.0f);
        if (*value)
        {
            drawList->AddLine(ImVec2(pos.x + 6.0f, boxY + 15.0f), ImVec2(pos.x + 12.0f, boxY + 21.0f), Theme::Base(), 2.5f);
            drawList->AddLine(ImVec2(pos.x + 12.0f, boxY + 21.0f), ImVec2(pos.x + 25.0f, boxY + 9.0f), Theme::Base(), 2.5f);
        }
        drawList->AddText(ImVec2(pos.x + boxSize + 16.0f, pos.y + (hitHeight - textSize.y) * 0.5f), labelColor, label);
    }

    void SliderFloat(ImDrawList* drawList, const char* id, ImVec2 pos, float width, const char* label, float* value, float min, float max, const char* format, float* visual)
    {
        char valueText[32]{};
        std::snprintf(valueText, sizeof(valueText), format, *value);

        ImGui::PushID(id);
        ImGui::SetCursorScreenPos(pos);
        ImGui::InvisibleButton("##slider", ImVec2(width, 34.0f));
        bool hovered = ImGui::IsItemHovered();
        bool active = ImGui::IsItemActive();
        if (active)
        {
            float t = Theme::Clamp((ImGui::GetIO().MousePos.x - pos.x) / width, 0.0f, 1.0f);
            *value = min + (max - min) * t;
        }
        ImGui::PopID();

        float t = Theme::Clamp((*value - min) / (max - min), 0.0f, 1.0f);
        *visual += (t - *visual) * (1.0f - std::exp(-ImGui::GetIO().DeltaTime * 18.0f));
        ImVec2 valueSize = ImGui::CalcTextSize(valueText);
        ImVec2 trackMin(pos.x, pos.y + 24.0f);
        ImVec2 trackMax(pos.x + width, pos.y + 32.0f);
        ImU32 labelColor = (hovered || active) ? Theme::Text() : Theme::MutedText();
        float fillX = trackMin.x + width * Theme::Clamp(*visual, 0.0f, 1.0f);

        drawList->AddText(pos, labelColor, label);
        drawList->AddText(ImVec2(pos.x + width - valueSize.x, pos.y), labelColor, valueText);
        drawList->AddRectFilled(trackMin, trackMax, Theme::Control(), 4.0f);
        drawList->AddRect(trackMin, trackMax, Theme::Outline(), 4.0f);
        drawList->AddRectFilled(trackMin, ImVec2(fillX > trackMin.x + 5.0f ? fillX : trackMin.x + 5.0f, trackMax.y), Theme::Accent(215), 4.0f);
        drawList->AddCircleFilled(ImVec2(fillX, (trackMin.y + trackMax.y) * 0.5f), active ? 6.0f : 5.0f, Theme::AccentSoft(), 24);
    }

    void SliderInt(ImDrawList* drawList, const char* id, ImVec2 pos, float width, const char* label, int* value, int min, int max, const char* format, float* visual)
    {
        char valueText[32]{};
        std::snprintf(valueText, sizeof(valueText), format, *value);

        ImGui::PushID(id);
        ImGui::SetCursorScreenPos(pos);
        ImGui::InvisibleButton("##slider", ImVec2(width, 34.0f));
        bool hovered = ImGui::IsItemHovered();
        bool active = ImGui::IsItemActive();
        if (active)
        {
            float t = Theme::Clamp((ImGui::GetIO().MousePos.x - pos.x) / width, 0.0f, 1.0f);
            *value = min + static_cast<int>((max - min) * t + 0.5f);
        }
        ImGui::PopID();

        float t = Theme::Clamp((static_cast<float>(*value) - static_cast<float>(min)) / static_cast<float>(max - min), 0.0f, 1.0f);
        *visual += (t - *visual) * (1.0f - std::exp(-ImGui::GetIO().DeltaTime * 18.0f));
        ImVec2 valueSize = ImGui::CalcTextSize(valueText);
        ImVec2 trackMin(pos.x, pos.y + 24.0f);
        ImVec2 trackMax(pos.x + width, pos.y + 32.0f);
        ImU32 labelColor = (hovered || active) ? Theme::Text() : Theme::MutedText();
        float fillX = trackMin.x + width * Theme::Clamp(*visual, 0.0f, 1.0f);

        drawList->AddText(pos, labelColor, label);
        drawList->AddText(ImVec2(pos.x + width - valueSize.x, pos.y), labelColor, valueText);
        drawList->AddRectFilled(trackMin, trackMax, Theme::Control(), 4.0f);
        drawList->AddRect(trackMin, trackMax, Theme::Outline(), 4.0f);
        drawList->AddRectFilled(trackMin, ImVec2(fillX > trackMin.x + 5.0f ? fillX : trackMin.x + 5.0f, trackMax.y), Theme::Accent(215), 4.0f);
        drawList->AddCircleFilled(ImVec2(fillX, (trackMin.y + trackMax.y) * 0.5f), active ? 6.0f : 5.0f, Theme::AccentSoft(), 24);
    }

    void Combo(ImDrawList* drawList, const char* id, ImVec2 pos, float rowWidth, const char* label, int* value, const char* const items[], int count)
    {
        static ImGuiID animatedComboId = 0;
        static float comboPopupAlpha = 0.0f;
        const float height = 32.0f;
        const float itemHeight = 30.0f;
        const float spacing = 6.0f;
        ImVec2 labelSize = ImGui::CalcTextSize(label);
        ImVec2 fieldMin(pos.x, pos.y + labelSize.y + spacing);
        ImVec2 fieldMax(fieldMin.x + rowWidth, fieldMin.y + height);

        ImGui::PushID(id);
        ImGuiID comboId = ImGui::GetID("##combo");
        ImGui::SetCursorScreenPos(fieldMin);
        ImGui::InvisibleButton("##combo", ImVec2(rowWidth, height));
        bool hovered = ImGui::IsItemHovered();
        bool popupOpen = ImGui::IsPopupOpen("##popup", ImGuiPopupFlags_None);
        if (ImGui::IsItemClicked())
        {
            ImGui::OpenPopup("##popup");
            animatedComboId = comboId;
            comboPopupAlpha = 0.0f;
            popupOpen = true;
        }
        if (popupOpen && animatedComboId != comboId)
        {
            animatedComboId = comboId;
            comboPopupAlpha = 0.0f;
        }
        if (animatedComboId == comboId)
        {
            float target = popupOpen ? 1.0f : 0.0f;
            comboPopupAlpha += (target - comboPopupAlpha) * (1.0f - std::exp(-ImGui::GetIO().DeltaTime * 18.0f));
        }

        drawList->AddText(pos, (hovered || popupOpen) ? Theme::Text() : Theme::MutedText(), label);
        drawList->AddRectFilled(fieldMin, fieldMax, Theme::Control(), 4.0f);
        drawList->AddRect(fieldMin, fieldMax, Theme::Outline(), 4.0f);
        drawList->AddText(ImVec2(fieldMin.x + 8.0f, fieldMin.y + (height - ImGui::GetFontSize()) * 0.5f - 1.0f), Theme::Text(), items[*value]);
        ImVec2 arrowSize = ImGui::CalcTextSize(u8"\uf107");
        drawList->AddText(ImVec2(fieldMax.x - 13.0f - arrowSize.x * 0.5f, fieldMin.y + (height - arrowSize.y) * 0.5f), popupOpen ? Theme::Accent(230) : Theme::MutedText(180), u8"\uf107");

        ImGui::SetNextWindowPos(ImVec2(fieldMin.x, fieldMax.y + 4.0f));
        ImGui::SetNextWindowSize(ImVec2(rowWidth, 8.0f + itemHeight * count));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(4.0f, 4.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0.0f, 0.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_PopupRounding, 3.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_Alpha, Theme::Clamp(comboPopupAlpha, 0.05f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_PopupBg, Theme::SurfaceVec());
        ImGui::PushStyleColor(ImGuiCol_Border, ImGui::ColorConvertU32ToFloat4(Theme::Outline()));
        if (ImGui::BeginPopup("##popup"))
        {
            ImDrawList* popupDrawList = ImGui::GetWindowDrawList();
            for (int i = 0; i < count; ++i)
            {
                ImVec2 itemPos = ImGui::GetCursorScreenPos();
                ImGui::InvisibleButton(items[i], ImVec2(rowWidth - 8.0f, itemHeight));
                bool itemHovered = ImGui::IsItemHovered();
                bool itemSelected = *value == i;
                if (itemHovered)
                {
                    popupDrawList->AddRectFilled(ImVec2(itemPos.x + 2.0f, itemPos.y + 2.0f), ImVec2(itemPos.x + rowWidth - 10.0f, itemPos.y + itemHeight - 2.0f), Theme::SurfaceLight(118), 3.0f);
                }
                popupDrawList->AddText(ImVec2(itemPos.x + 8.0f, itemPos.y + (itemHeight - ImGui::GetFontSize()) * 0.5f - 1.0f), itemSelected ? Theme::Accent() : itemHovered ? Theme::Text() : Theme::MutedText(), items[i]);
                if (ImGui::IsItemClicked())
                {
                    *value = i;
                    ImGui::CloseCurrentPopup();
                }
            }
            ImGui::EndPopup();
        }
        ImGui::PopStyleColor(2);
        ImGui::PopStyleVar(4);
        ImGui::PopID();
    }

    void Input(ImDrawList* drawList, const char* id, ImVec2 pos, float width, const char* label, char* buffer, std::size_t bufferSize)
    {
        drawList->AddText(pos, Theme::MutedText(), label);
        ImGui::PushID(id);
        ImGui::SetCursorScreenPos(ImVec2(pos.x, pos.y + 22.0f));
        ImGui::SetNextItemWidth(width);
        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 3.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(8.0f, 5.0f));
        ImGui::PushStyleColor(ImGuiCol_FrameBg, Theme::ControlVec());
        ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, Theme::SurfaceVec());
        ImGui::PushStyleColor(ImGuiCol_FrameBgActive, ImVec4(0.055f, 0.235f, 0.42f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_Text, ImGui::ColorConvertU32ToFloat4(Theme::Text()));
        ImGui::InputText("##input", buffer, bufferSize);
        ImGui::PopStyleColor(4);
        ImGui::PopStyleVar(2);
        ImGui::PopID();
    }

    void ColorPicker(ImDrawList* drawList, const char* id, ImVec2 pos, float rowWidth, const char* label, float color[4])
    {
        static ImGuiID animatedColorId = 0;
        static float colorPopupAnim = 0.0f;
        ImVec2 textSize = ImGui::CalcTextSize(label);
        ImVec2 swatchMin(pos.x + rowWidth - 40.0f, pos.y - 4.0f);
        if (!(label[0] == '#' && label[1] == '#'))
        {
            drawList->AddText(ImVec2(pos.x, pos.y + (18.0f - textSize.y) * 0.5f), Theme::MutedText(), label);
        }

        ImGui::PushID(id);
        ImGuiID colorId = ImGui::GetID("##color");
        ImGui::SetCursorScreenPos(swatchMin);
        ImGui::ColorButton("##button", ImVec4(color[0], color[1], color[2], color[3]), ImGuiColorEditFlags_AlphaPreviewHalf | ImGuiColorEditFlags_NoTooltip, ImVec2(34.0f, 24.0f));
        if (ImGui::IsItemClicked())
        {
            ImGui::OpenPopup("##popup");
            animatedColorId = colorId;
            colorPopupAnim = 0.0f;
        }
        bool colorPopupOpen = ImGui::IsPopupOpen("##popup", ImGuiPopupFlags_None);
        if (colorPopupOpen && animatedColorId != colorId)
        {
            animatedColorId = colorId;
            colorPopupAnim = 0.0f;
        }
        if (animatedColorId == colorId)
        {
            float target = colorPopupOpen ? 1.0f : 0.0f;
            colorPopupAnim += (target - colorPopupAnim) * (1.0f - std::exp(-ImGui::GetIO().DeltaTime * 18.0f));
        }
        drawList->AddRect(swatchMin, ImVec2(swatchMin.x + 34.0f, swatchMin.y + 24.0f), Theme::Outline(), 4.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8.0f, 8.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_PopupRounding, 4.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_Alpha, Theme::Clamp(colorPopupAnim, 0.05f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_PopupBg, Theme::SurfaceVec());
        ImGui::PushStyleColor(ImGuiCol_Border, ImGui::ColorConvertU32ToFloat4(Theme::Outline()));
        if (ImGui::BeginPopup("##popup"))
        {
            ImGui::ColorPicker4("##picker", color, ImGuiColorEditFlags_NoSidePreview | ImGuiColorEditFlags_AlphaBar | ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_AlphaPreview);
            ImGui::EndPopup();
        }
        ImGui::PopStyleColor(2);
        ImGui::PopStyleVar(3);
        ImGui::PopID();
    }
}
