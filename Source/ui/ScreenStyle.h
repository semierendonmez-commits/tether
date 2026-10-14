// How the norns screen's sixteen levels are coloured in the mirror, the snapshots and the GIFs.
// Independent of the panel theme, though most styles take their colours from it.
#pragma once

#include "Themes.h"

namespace ui
{

enum ScreenStyle { StyleInk, StyleThermal, StyleDuotone, StyleSpectrum, StyleOled, StylePaper, kNumScreenStyles };

inline const char* screenStyleName (int s)
{
    static const char* n[] = { "INK", "THERMAL", "DUOTONE", "SPECTRUM", "OLED", "PAPER" };
    return n[juce::jlimit (0, kNumScreenStyles - 1, s)];
}

inline const char* screenStyleHint (int s)
{
    static const char* n[] = {
        "the theme's ink on the theme's screen, one colour",
        "the theme's false-colour ramp: dim levels cold, bright levels hot",
        "two of the theme's colours: dim levels one, bright levels the other",
        "a hue for every level, dark blue to white",
        "as on the norns: grey levels on black",
        "black on white, like a printout",
    };
    return n[juce::jlimit (0, kNumScreenStyles - 1, s)];
}

struct ScreenPalette
{
    juce::Colour level[16];
    juce::Colour bg, grid, text;   // the well, the gaps between pixels, status text on the well
};

inline ScreenPalette screenPalette (const Theme& t, int style)
{
    ScreenPalette p;
    auto lerp = [] (juce::Colour a, juce::Colour b, float f) { return a.interpolatedWith (b, juce::jlimit (0.0f, 1.0f, f)); };
    switch (juce::jlimit (0, kNumScreenStyles - 1, style))
    {
        case StyleThermal:
            p.bg = t.light ? juce::Colour (0xff05060a) : t.screen;
            for (int k = 1; k < 16; ++k) p.level[k] = t.thermal (0.08f + 0.92f * (float) k / 15.0f);
            break;
        case StyleDuotone:
        {
            p.bg = t.light ? juce::Colour (0xff07080c) : t.screen;
            const auto lo = t.accent2, hi = t.accent.brighter (0.25f);
            for (int k = 1; k < 16; ++k)
            {
                const float f = (float) k / 15.0f;
                p.level[k] = f < 0.5f ? lerp (p.bg, lo, 0.25f + 1.5f * f) : lerp (lo, hi, (f - 0.5f) * 2.0f);
            }
            break;
        }
        case StyleSpectrum:
            p.bg = juce::Colour (0xff04040a);
            for (int k = 1; k < 16; ++k)
            {
                const float f = (float) k / 15.0f;
                p.level[k] = f > 0.97f ? juce::Colours::white
                                       : juce::Colour::fromHSV (0.68f - 0.80f * f < 0.0f ? 0.68f - 0.80f * f + 1.0f : 0.68f - 0.80f * f,
                                                                0.95f - 0.35f * f * f, 0.35f + 0.65f * f, 1.0f);
            }
            break;
        case StyleOled:
            p.bg = juce::Colours::black;
            for (int k = 1; k < 16; ++k) p.level[k] = juce::Colour::greyLevel (std::pow ((float) k / 15.0f, 0.8f));
            break;
        case StylePaper:
            p.bg = juce::Colours::white;
            for (int k = 1; k < 16; ++k) p.level[k] = juce::Colour::greyLevel (1.0f - std::pow ((float) k / 15.0f, 0.75f));
            break;
        default:   // StyleInk
            p.bg = t.screen;
            for (int k = 1; k < 16; ++k) p.level[k] = lerp (t.screen, t.ink, std::pow ((float) k / 15.0f, 0.85f));
            break;
    }
    p.level[0] = p.bg;
    const bool lightBg = p.bg.getPerceivedBrightness() > 0.5f;
    p.grid = lightBg ? p.bg.darker (0.06f) : p.bg.brighter (0.06f);
    p.text = lightBg ? juce::Colours::black : (style == StyleInk ? t.ink : p.level[13]);
    return p;
}

} // namespace ui
