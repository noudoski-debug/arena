#pragma once

struct ImFont;

namespace MenuEternalFont
{
    ImFont* LoadSemiBold(float sizePixels);
    ImFont* LoadIcons(float sizePixels, bool mergeWithLastFont = true);
}
