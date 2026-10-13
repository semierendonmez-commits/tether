// Visual language shared with the other plug-ins: Early Cyber / Corporate
// Cyberspace, mid-80s to mid-90s.
//   - workstation window chrome: bevels, pinstriped title bars, close boxes
//     (Macintosh System 6/7, SGI Indigo Magic, NeXTSTEP, Windows 3.1)
//   - thermographic false colour and ordered dither from first-wave paint
//     and DTP software (Wired '93, The Mind's Eye)
//   - April Greiman / Pacific Wave: translucent planes stacked at angles
//   - Corporate Gen-X Cyber: the extended underline, acid accents
// Type: Michroma for labels, VT323 for every readout that sits on a screen.
//
// Carries the legibility fixes made after the JDAT review: title text sits on
// a panel-coloured plate, label sizes 8 / 8.5 px instead of 7.5.
#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "BinaryData.h"
#include "Themes.h"

namespace ui
{

struct Fonts
{
    juce::Typeface::Ptr label, screen;
    Fonts()
    {
        label  = juce::Typeface::createSystemTypefaceFor (BinaryData::MichromaRegular_ttf, BinaryData::MichromaRegular_ttfSize);
        screen = juce::Typeface::createSystemTypefaceFor (BinaryData::VT323Regular_ttf, BinaryData::VT323Regular_ttfSize);
    }
};

inline juce::Font labelFont (float h)
{
    juce::SharedResourcePointer<Fonts> f;
    return juce::Font (juce::FontOptions (f->label).withHeight (h));
}

inline juce::Font screenFont (float h)
{
    juce::SharedResourcePointer<Fonts> f;
    return juce::Font (juce::FontOptions (f->screen).withHeight (h));
}

// Shared by every widget of one editor, so two instances can wear different themes.
struct Skin
{
    int index = 0;
    bool motion = true;
    const Theme& t() const { return theme (index); }
};

//==============================================================================
namespace draw
{
    inline int bayer (int x, int y)
    {
        static const int m[4][4] { { 0, 8, 2, 10 }, { 12, 4, 14, 6 }, { 3, 11, 1, 9 }, { 15, 7, 13, 5 } };
        return m[y & 3][x & 3];
    }

    inline void bevel (juce::Graphics& g, juce::Rectangle<int> r, const Theme& t, bool raised, int w = 1)
    {
        const auto hi = raised ? t.panelHi : t.panelLo;
        const auto lo = raised ? t.panelLo : t.panelHi;
        for (int i = 0; i < w; ++i)
        {
            auto q = r.reduced (i);
            g.setColour (hi);
            g.fillRect (q.getX(), q.getY(), q.getWidth() - 1, 1);
            g.fillRect (q.getX(), q.getY(), 1, q.getHeight() - 1);
            g.setColour (lo);
            g.fillRect (q.getX() + 1, q.getBottom() - 1, q.getWidth() - 1, 1);
            g.fillRect (q.getRight() - 1, q.getY() + 1, 1, q.getHeight() - 1);
        }
    }

    inline void screenWell (juce::Graphics& g, juce::Rectangle<int> r, const Theme& t)
    {
        g.setColour (t.panelLo);
        g.drawRect (r.expanded (1));
        bevel (g, r.expanded (2), t, false);
        g.setColour (t.screen);
        g.fillRect (r);
    }

    inline void scanlines (juce::Graphics& g, juce::Rectangle<int> r, const Theme&, float alpha = 0.18f)
    {
        g.setColour (juce::Colours::black.withAlpha (alpha));
        for (int y = r.getY() + 1; y < r.getBottom(); y += 2)
            g.fillRect (r.getX(), y, r.getWidth(), 1);
    }

    // Workstation window: outer line, raised bevel, pinstriped title bar, close
    // box, title on a plate. Returns the content area.
    inline juce::Rectangle<int> window (juce::Graphics& g, juce::Rectangle<int> r, const Theme& t,
                                        const juce::String& title, const juce::String& info = {})
    {
        g.setColour (juce::Colours::black.withAlpha (0.35f));
        g.fillRect (r.translated (4, 4));
        g.setColour (t.panelLo.darker (0.4f));
        g.fillRect (r);
        auto in = r.reduced (1);
        g.setColour (t.panel);
        g.fillRect (in);
        bevel (g, in, t, true, 2);

        auto bar = in.reduced (3).removeFromTop (17);
        g.setColour (t.titleA);
        g.fillRect (bar);
        g.setColour (t.titleB);
        for (int y = bar.getY() + 3; y < bar.getBottom() - 2; y += 2)
            g.fillRect (bar.getX() + 2, y, bar.getWidth() - 4, 1);

        auto box = juce::Rectangle<int> (bar.getX() + 6, bar.getY() + 3, 11, 11);
        g.setColour (t.titleA);
        g.fillRect (box.expanded (3, 1));
        g.setColour (t.titleText);
        g.drawRect (box);
        g.setColour (t.titleB);
        g.fillRect (box.reduced (3));

        auto f = labelFont (9.5f);
        const int tw = juce::GlyphArrangement::getStringWidthInt (f, title) + 18;
        auto plate = juce::Rectangle<int> (tw, bar.getHeight()).withCentre (bar.getCentre());
        g.setColour (t.panel);
        g.fillRect (plate);
        g.setColour (t.text);
        g.setFont (f);
        g.drawText (title, plate, juce::Justification::centred);

        if (info.isNotEmpty())
        {
            auto sf = screenFont (15.0f);
            const int iw = juce::GlyphArrangement::getStringWidthInt (sf, info) + 10;
            auto ip = juce::Rectangle<int> (bar.getRight() - iw - 4, bar.getY(), iw, bar.getHeight());
            g.setColour (t.panel);
            g.fillRect (ip);
            g.setColour (t.text);
            g.setFont (sf);
            g.drawText (info, ip, juce::Justification::centred);
        }

        return in.reduced (3).withTrimmedTop (19);
    }

    inline void smallLabel (juce::Graphics& g, const juce::String& s, juce::Rectangle<int> r, const Theme& t,
                            juce::Justification j = juce::Justification::centredLeft)
    {
        g.setFont (labelFont (8.0f));
        g.setColour (t.textDim);
        g.drawText (s, r, j, false);
    }

    // Kare-style 1-bit icon, rows of '#'.
    inline void icon (juce::Graphics& g, const char* const* rows, int n, juce::Rectangle<float> r, juce::Colour col)
    {
        const float s = juce::jmin (r.getWidth(), r.getHeight()) / (float) n;
        const float ox = r.getCentreX() - s * (float) n * 0.5f, oy = r.getCentreY() - s * (float) n * 0.5f;
        g.setColour (col);
        for (int y = 0; y < n; ++y)
            for (int x = 0; rows[y][x] != 0; ++x)
                if (rows[y][x] == '#') g.fillRect (ox + (float) x * s, oy + (float) y * s, s, s);
    }
} // namespace draw

//==============================================================================
class Look : public juce::LookAndFeel_V4
{
public:
    explicit Look (Skin& s) : skin (s) {}

    juce::Font getPopupMenuFont() override { return screenFont (19.0f); }

    void drawPopupMenuBackground (juce::Graphics& g, int w, int h) override
    {
        const auto& t = skin.t();
        g.fillAll (t.panel);
        draw::bevel (g, { 0, 0, w, h }, t, true, 2);
        g.setColour (t.panelLo.darker (0.4f));
        g.drawRect (0, 0, w, h);
    }

    void drawPopupMenuSectionHeader (juce::Graphics& g, const juce::Rectangle<int>& area, const juce::String& text) override
    {
        const auto& t = skin.t();
        g.setColour (t.textDim);
        g.setFont (labelFont (8.5f));
        g.drawText (text, area.reduced (10, 0), juce::Justification::bottomLeft);
    }

    void drawPopupMenuItem (juce::Graphics& g, const juce::Rectangle<int>& area, bool isSeparator, bool isActive,
                            bool isHighlighted, bool isTicked, bool hasSubMenu, const juce::String& text,
                            const juce::String&, const juce::Drawable*, const juce::Colour*) override
    {
        const auto& t = skin.t();
        if (isSeparator)
        {
            g.setColour (t.panelLo);
            g.fillRect (area.reduced (6, 0).withHeight (1).withY (area.getCentreY()));
            g.setColour (t.panelHi);
            g.fillRect (area.reduced (6, 0).withHeight (1).withY (area.getCentreY() + 1));
            return;
        }
        auto r = area.reduced (3, 0);
        if (isHighlighted && isActive)
        {
            g.setColour (t.titleA);
            g.fillRect (r);
        }
        const auto fg = isHighlighted && isActive ? t.titleText : (isActive ? t.text : t.textDim);
        if (isTicked)
        {
            g.setColour (isHighlighted ? t.titleB : t.accent);
            g.fillRect (r.getX() + 6, r.getCentreY() - 3, 7, 7);
        }
        g.setColour (fg);
        g.setFont (screenFont (19.0f));
        g.drawText (text, r.withTrimmedLeft (20), juce::Justification::centredLeft);
        if (hasSubMenu)
        {
            juce::Path tri;
            const float cx = (float) r.getRight() - 9.0f, cy = (float) r.getCentreY();
            tri.addTriangle (cx - 3.0f, cy - 4.0f, cx - 3.0f, cy + 4.0f, cx + 2.0f, cy);
            g.fillPath (tri);
        }
    }

    void getIdealPopupMenuItemSize (const juce::String& text, bool isSeparator, int, int& w, int& h) override
    {
        if (isSeparator) { w = 50; h = 8; return; }
        w = juce::GlyphArrangement::getStringWidthInt (screenFont (19.0f), text) + 44;
        h = 20;
    }

    int getPopupMenuBorderSize() override { return 3; }

    // ---- the fixes carried over from the sibling plug-ins: themed tooltips, alerts, scrollbars
    void drawTooltip (juce::Graphics& g, const juce::String& text, int w, int h) override
    {
        const auto& t = skin.t();
        g.fillAll (t.screen);
        g.setColour (t.panelLo.darker (0.4f));
        g.drawRect (0, 0, w, h);
        g.setColour (t.ink.withAlpha (0.5f));
        g.drawRect (1, 1, w - 2, h - 2);
        g.setColour (t.ink);
        g.setFont (screenFont (17.0f));
        g.drawFittedText (text, juce::Rectangle<int> (w, h).reduced (8, 4), juce::Justification::centredLeft, 4);
    }

    juce::Rectangle<int> getTooltipBounds (const juce::String& text, juce::Point<int> pos, juce::Rectangle<int> parent) override
    {
        const int tw = juce::jmin (460, juce::GlyphArrangement::getStringWidthInt (screenFont (17.0f), text) + 20);
        const int lines = 1 + juce::GlyphArrangement::getStringWidthInt (screenFont (17.0f), text) / 440;
        const int th = 8 + 17 * lines;
        return juce::Rectangle<int> (pos.x > parent.getCentreX() ? pos.x - (tw + 12) : pos.x + 18,
                                     pos.y > parent.getCentreY() ? pos.y - (th + 6) : pos.y + 8, tw, th).constrainedWithin (parent);
    }

    void drawAlertBox (juce::Graphics& g, juce::AlertWindow& a, const juce::Rectangle<int>& textArea, juce::TextLayout& layout) override
    {
        const auto& t = skin.t();
        auto r = a.getLocalBounds();
        g.fillAll (t.panel);
        draw::bevel (g, r, t, true, 2);
        g.setColour (t.panelLo.darker (0.5f));
        g.drawRect (r);
        auto bar = r.reduced (3).removeFromTop (17);
        g.setColour (t.titleA);
        g.fillRect (bar);
        g.setColour (t.titleB);
        for (int y = bar.getY() + 3; y < bar.getBottom() - 2; y += 2) g.fillRect (bar.getX() + 2, y, bar.getWidth() - 4, 1);
        g.setColour (t.text);
        layout.draw (g, textArea.toFloat());
    }

    juce::Font getAlertWindowTitleFont() override { return labelFont (12.0f); }
    juce::Font getAlertWindowMessageFont() override { return screenFont (19.0f); }
    juce::Font getAlertWindowFont() override { return screenFont (19.0f); }
    juce::Font getTextButtonFont (juce::TextButton&, int) override { return screenFont (18.0f); }

    void drawButtonBackground (juce::Graphics& g, juce::Button& b, const juce::Colour&, bool over, bool down) override
    {
        const auto& t = skin.t();
        auto r = b.getLocalBounds();
        g.setColour (t.panelLo.darker (0.5f));
        g.drawRect (r);
        g.setColour (over ? t.panel.brighter (0.08f) : t.panel);
        g.fillRect (r.reduced (1));
        draw::bevel (g, r.reduced (1), t, ! down, 2);
    }

    void drawButtonText (juce::Graphics& g, juce::TextButton& b, bool, bool down) override
    {
        g.setColour (skin.t().text);
        g.setFont (screenFont (18.0f));
        g.drawText (b.getButtonText(), b.getLocalBounds().translated (down ? 1 : 0, down ? 1 : 0), juce::Justification::centred);
    }

    void drawScrollbar (juce::Graphics& g, juce::ScrollBar&, int x, int y, int w, int h, bool vertical,
                        int thumbStart, int thumbSize, bool over, bool) override
    {
        const auto& t = skin.t();
        g.setColour (t.screen);
        g.fillRect (x, y, w, h);
        auto th = vertical ? juce::Rectangle<int> (x + 1, thumbStart, w - 2, thumbSize) : juce::Rectangle<int> (thumbStart, y + 1, thumbSize, h - 2);
        g.setColour (over ? t.panel.brighter (0.1f) : t.panel);
        g.fillRect (th);
        draw::bevel (g, th, t, true, 1);
    }

    void drawCallOutBoxBackground (juce::CallOutBox&, juce::Graphics& g, const juce::Path& path, juce::Image&) override
    {
        const auto& t = skin.t();
        g.setColour (juce::Colours::black.withAlpha (0.4f));
        g.fillPath (path, juce::AffineTransform::translation (3.0f, 4.0f));
        g.setColour (t.panel);
        g.fillPath (path);
        g.setColour (t.panelLo.darker (0.4f));
        g.strokePath (path, juce::PathStrokeType (2.0f));
    }
    int getCallOutBoxBorderSize (const juce::CallOutBox&) override { return 14; }

    void fillTextEditorBackground (juce::Graphics& g, int w, int h, juce::TextEditor&) override
    {
        const auto& t = skin.t();
        g.fillAll (t.screen);
        g.setColour (t.panelLo);
        g.drawRect (0, 0, w, h);
    }
    void drawTextEditorOutline (juce::Graphics&, int, int, juce::TextEditor&) override {}

    void refreshColours()
    {
        const auto& t = skin.t();
        setColour (juce::TextEditor::textColourId, t.ink);
        setColour (juce::TextEditor::highlightColourId, t.accent.withAlpha (0.4f));
        setColour (juce::CaretComponent::caretColourId, t.ink);
        setColour (juce::AlertWindow::textColourId, t.text);
        setColour (juce::AlertWindow::backgroundColourId, t.panel);
        setColour (juce::TextButton::textColourOffId, t.text);
        setColour (juce::ResizableWindow::backgroundColourId, t.panel);
    }

private:
    Skin& skin;
};

} // namespace ui
