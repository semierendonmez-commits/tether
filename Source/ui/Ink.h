// Colour helpers shared by the views (from the sibling plug-ins): text that stays
// legible on light and dark screens, and one colour per transient class.
#pragma once

#include "Themes.h"

namespace ui
{

inline juce::String hex2 (int v) { return juce::String::toHexString (v & 0xFF).paddedLeft ('0', 2).toUpperCase(); }

inline float luma (juce::Colour c) { return 0.299f * c.getFloatRed() + 0.587f * c.getFloatGreen() + 0.114f * c.getFloatBlue(); }

// keeps a colour legible on the screen colour of the theme (light screens included)
inline juce::Colour readable (const Theme& t, juce::Colour c)
{
    const float d = std::abs (luma (c) - luma (t.screen));
    return d >= 0.32f ? c : c.interpolatedWith (t.ink, 0.75f);
}

// a dim that still reads: on light screens the theme's inkDim is already the right weight
inline juce::Colour dim (const Theme& t) { return luma (t.screen) > 0.5f ? t.inkDim.darker (0.3f) : t.inkDim.brighter (0.6f); }

// K S G H
inline juce::Colour clsColour (const Theme& t, int c)
{
    switch (c & 3)
    {
        case 0:  return readable (t, t.thermal (0.95f));
        case 1:  return readable (t, t.accent2);
        case 2:  return readable (t, t.thermal (0.55f));
        default: return readable (t, t.ink.interpolatedWith (t.inkDim, 0.25f));
    }
}

} // namespace ui
