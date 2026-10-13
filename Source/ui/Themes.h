// The thirteen themes shared with the sibling plug-ins: the ten of Call & Response,
// Acid, JDAT, IKEDAR and MODPORT, plus Breakbeat's Wired '93, Emigre and Sunset '95.
#pragma once

#include <juce_graphics/juce_graphics.h>
#include <array>

namespace ui
{

struct Theme
{
    const char* name;
    juce::Colour desk, panel, panelHi, panelLo;
    juce::Colour titleA, titleB, titleText;
    juce::Colour screen, ink, inkDim;
    juce::Colour text, textDim;
    juce::Colour accent, accent2;
    std::array<juce::Colour, 6> ramp;   // thermal false colour, cold -> hot
    bool light;

    juce::Colour thermal (float t) const
    {
        t = juce::jlimit (0.0f, 1.0f, t) * 5.0f;
        const int i = juce::jmin (4, (int) t);
        return ramp[(size_t) i].interpolatedWith (ramp[(size_t) i + 1], t - (float) i);
    }
};

inline juce::Colour c (juce::uint32 rgb) { return juce::Colour (0xff000000u | rgb); }

inline const std::array<Theme, 13>& themes()
{
    static const std::array<Theme, 13> t {{
        { "cyber",
          c (0x0b0d12), c (0x272a33), c (0x4b505e), c (0x0f1116),
          c (0x15171e), c (0xe8401c), c (0xf2f4f8),
          c (0x05070a), c (0xff6a2b), c (0x4a1e0c),
          c (0xd9dde6), c (0x868c9c),
          c (0xff5a1f), c (0x3ad1ff),
          {{ c (0x0a0030), c (0x3b0a8c), c (0xc3187d), c (0xff5a1f), c (0xffd23f), c (0xfffbe0) }}, false },

        { "terminal",
          c (0x010603), c (0x0b1a0f), c (0x1f3d27), c (0x020503),
          c (0x06120a), c (0x1c8a3a), c (0xb8ffc8),
          c (0x010801), c (0x39ff6a), c (0x0e4019),
          c (0x8dffa8), c (0x3f8a52),
          c (0x39ff6a), c (0xffb000),
          {{ c (0x001a05), c (0x00401a), c (0x00802b), c (0x22c94a), c (0x7dff8f), c (0xe0ffe6) }}, false },

        { "amber",
          c (0x080501), c (0x1c130a), c (0x3c2a15), c (0x060402),
          c (0x120c05), c (0xb06a00), c (0xffe2a0),
          c (0x070400), c (0xffb000), c (0x4a3000),
          c (0xffc54a), c (0x9a6e1c),
          c (0xffb000), c (0xff7a00),
          {{ c (0x140800), c (0x4a2200), c (0x8a4a00), c (0xd27a00), c (0xffb000), c (0xffe7a0) }}, false },

        { "ocean",
          c (0x02101f), c (0x0b2a4a), c (0x1d4c7a), c (0x020b18),
          c (0x061a33), c (0x0f6a8a), c (0xe0fbff),
          c (0x01101f), c (0x3ff0e0), c (0x0f4a5a),
          c (0xcfe9ff), c (0x6f9cc4),
          c (0x3ff0e0), c (0xff7b6b),
          {{ c (0x00102a), c (0x00306a), c (0x0070a8), c (0x20c0c8), c (0x7ff0d8), c (0xffe8d0) }}, false },

        { "ice",
          c (0xb9c4cf), c (0xe2e8ef), c (0xffffff), c (0x8794a4),
          c (0x9fb0c4), c (0x1b4fbf), c (0xffffff),
          c (0x0f1c33), c (0x9fe0ff), c (0x2c4a6e),
          c (0x1a2638), c (0x55657a),
          c (0x1b4fbf), c (0xe0306a),
          {{ c (0x0f1c33), c (0x1b4fbf), c (0x3f8fe0), c (0x8fd0f0), c (0xd8f2ff), c (0xffffff) }}, true },

        { "indigo",
          c (0x56627e), c (0xaab4c8), c (0xdde2ec), c (0x5c6680),
          c (0x8a94ac), c (0x5a3f9e), c (0xfff8c8),
          c (0x1a1530), c (0xfff6a8), c (0x5a4f80),
          c (0x141a28), c (0x3f4860),
          c (0x6a3fb0), c (0xe8d050),
          {{ c (0x1a1030), c (0x4a2a8a), c (0x8a5ad0), c (0xc8a0f0), c (0xfff0a0), c (0xffffe0) }}, true },

        { "next",
          c (0x4a4a4a), c (0xaaaaaa), c (0xe2e2e2), c (0x555555),
          c (0x000000), c (0x000000), c (0xffffff),
          c (0x000000), c (0xe8e8e8), c (0x4a4a4a),
          c (0x000000), c (0x3a3a3a),
          c (0x000000), c (0x555555),
          {{ c (0x000000), c (0x333333), c (0x666666), c (0x999999), c (0xcccccc), c (0xffffff) }}, true },

        { "cyberdelia",
          c (0x0b0016), c (0x22073a), c (0x4a1a70), c (0x07000f),
          c (0x14022a), c (0xd0209a), c (0xfff0ff),
          c (0x08000f), c (0xff3fd0), c (0x4a0f50),
          c (0xf0d8ff), c (0x9a70c0),
          c (0xff3fd0), c (0x3ff6ff),
          {{ c (0x2a00ff), c (0x00c8ff), c (0x00ff8a), c (0xffee00), c (0xff6a00), c (0xff0080) }}, false },

        { "mars",
          c (0x170803), c (0x5a2412), c (0x8a4024), c (0x200a04),
          c (0x2a0e05), c (0xd0501a), c (0xfff0d8),
          c (0x120502), c (0xffb070), c (0x5a2a10),
          c (0xf6d6b0), c (0xb07a58),
          c (0xff6a1f), c (0xe8c890),
          {{ c (0x200600), c (0x6a1400), c (0xb03a08), c (0xe8761f), c (0xf0b060), c (0xfff0d0) }}, false },

        { "handheld",
          c (0x306230), c (0x8bac0f), c (0x9bbc0f), c (0x306230),
          c (0x306230), c (0x0f380f), c (0x9bbc0f),
          c (0x9bbc0f), c (0x0f380f), c (0x6f9a2a),
          c (0x0f380f), c (0x306230),
          c (0x0f380f), c (0x306230),
          {{ c (0x0f380f), c (0x306230), c (0x5a8a1f), c (0x8bac0f), c (0x9bbc0f), c (0xc4dc5a) }}, true },

        { "wired '93",
          c (0x000000), c (0x161616), c (0x3a3a3a), c (0x050505),
          c (0x0e0e0e), c (0xff2d95), c (0xfff200),
          c (0x000000), c (0xfff200), c (0x4a4500),
          c (0xfff200), c (0xb0a640),
          c (0xff2d95), c (0x00e5ff),
          {{ c (0x1a0030), c (0xff2d95), c (0xff7a00), c (0xfff200), c (0xaaff00), c (0x00e5ff) }}, false },

        { "emigre",
          c (0xe8dc2a), c (0xf0e430), c (0xfffbb0), c (0x8a8010),
          c (0x111111), c (0xe8dc2a), c (0xf0e430),
          c (0x0b0b08), c (0xf0e430), c (0x4e4a1a),
          c (0x111111), c (0x5a5520),
          c (0xff3b1a), c (0x1fa8ff),
          {{ c (0x1a1a10), c (0x6a6420), c (0xc8bc2a), c (0xf0e430), c (0xff3b1a), c (0xffffff) }}, true },

        { "sunset '95",
          c (0x1a0a2a), c (0x3d1f5c), c (0x7a4aa8), c (0x12061e),
          c (0x2a1040), c (0xff3d8b), c (0xffe0c8),
          c (0x0d0518), c (0xff8a1e), c (0x4a2a5a),
          c (0xffe0c8), c (0xd090a0),
          c (0xff8a1e), c (0xffc23d),
          {{ c (0x1a0634), c (0x5a1a8a), c (0xd0307a), c (0xff6a3a), c (0xffb23a), c (0xfff0b0) }}, false },
    }};
    return t;
}

inline int numThemes() { return (int) themes().size(); }
inline const Theme& theme (int i) { return themes()[(size_t) juce::jlimit (0, numThemes() - 1, i)]; }

} // namespace ui
