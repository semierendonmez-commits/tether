// Widgets shared with the sibling plug-ins (same drawing, same gestures). In FLOATER the
// modulation glow on a dial is the CV the pin matrix sends to it; locks keep it from NEW PATCH.
#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "Look.h"

namespace ui
{

//==============================================================================
// What widgets ask the editor: locks, modulation, the modulation menu.
struct Host
{
    virtual ~Host() = default;
    virtual bool isLocked (const juce::String& id) const = 0;
    virtual void setLocked (const juce::String& id, bool) = 0;
    virtual bool modActive (const juce::String& id) const = 0;
    virtual float modValue (const juce::String& id) const = 0;             // normalised, LFOs applied
    virtual bool canModulate (const juce::String& id) const = 0;
    virtual void addMod (int lfo, const juce::String& id) = 0;
    virtual void modMenu (juce::PopupMenu& m, const juce::String& id) = 0;  // adds the routing items
    virtual void hint (const juce::String&) = 0;
    virtual bool hasLocks() const { return true; }        // EPOCH has nothing to randomize: no lock item
};

namespace draw
{
    inline void padlock (juce::Graphics& g, juce::Rectangle<int> r, juce::Colour c)
    {
        static const char* rows[] = { "..###..", ".#...#.", ".#...#.", "#######", "###.###", "###.###", "#######" };
        icon (g, rows, 7, r.toFloat(), c);
    }

    // right-click menu of any control: lock from random (+ the LFO routings of a dial)
    inline void contextMenu (Host* host, const juce::String& id, juce::Component* target)
    {
        if (host == nullptr) return;
        juce::PopupMenu m;
        if (host->hasLocks()) m.addItem (1, "lock from random", true, host->isLocked (id));
        host->modMenu (m, id);
        if (m.getNumItems() == 0) return;
        juce::Component::SafePointer<juce::Component> safe (target);
        m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (target),
                         [host, id, safe] (int r)
                         {
                             if (r == 1 && safe != nullptr) { host->setLocked (id, ! host->isLocked (id)); safe->repaint(); }
                         });
    }
}

//==============================================================================
// Rotary control: ring of pixel blocks lit along the thermal ramp, bevelled
// octagonal cap, value readout in a screen well. With an LFO on it the travel
// from the set value to the live one glows. Drop an LFO tile on it to route it.
class Dial : public juce::Component, public juce::SettableTooltipClient, public juce::DragAndDropTarget
{
public:
    Dial (Skin& s, juce::RangedAudioParameter& p, juce::String lbl, Host* h = nullptr, bool compact = false)
        : skin (s), param (p), label (std::move (lbl)), host (h), small (compact),
          attach (p, [this] (float) { repaint(); }, nullptr)
    {
        attach.sendInitialUpdate();
        setRepaintsOnMouseActivity (true);
    }

    juce::String paramId() const { return param.getParameterID(); }
    bool modulated() const { return host != nullptr && host->modActive (paramId()); }
    void setLabel (const juce::String& l) { if (l != label) { label = l; repaint(); } }

    void paint (juce::Graphics& g) override
    {
        const auto& t = skin.t();
        auto r = getLocalBounds();
        const bool live = modulated();
        const float v = param.getValue();
        const float mv = live ? host->modValue (paramId()) : v;

        auto top = r.removeFromTop (12);
        draw::smallLabel (g, label, top, t, juce::Justification::centred);
        if (host != nullptr && host->isLocked (paramId()))
            draw::padlock (g, top.removeFromRight (9).withSizeKeepingCentre (8, 8), t.accent);

        auto read = r.removeFromBottom (small ? 15 : 17).reduced (small ? 1 : 3, 1);
        auto area = r.toFloat().reduced (2.0f);
        const float d = juce::jmin (area.getWidth(), area.getHeight());
        auto sq = juce::Rectangle<float> (d, d).withCentre (area.getCentre());

        if (live || dropHover)
        {
            g.setColour (t.accent2.withAlpha (dropHover ? 0.35f : 0.12f + 0.25f * std::abs (mv - v)));
            g.fillRect (sq.expanded (3.0f));
            g.setColour (t.accent2);
            g.drawRect (sq.expanded (3.0f), 1.0f);
            if (live)
            {
                auto badge = juce::Rectangle<float> (sq.getX() - 3.0f, sq.getY() - 3.0f, 16.0f, 10.0f);
                g.fillRect (badge);
                g.setColour (t.screen);
                g.setFont (screenFont (12.0f));
                g.drawText ("CV", badge, juce::Justification::centred);
            }
        }

        const float a0 = juce::MathConstants<float>::pi * 1.25f, span = juce::MathConstants<float>::pi * 1.5f;
        constexpr int N = 19;
        const float ringR = d * 0.5f - 3.0f, blk = juce::jmax (2.0f, d * 0.075f);
        const float lo = juce::jmin (v, mv), hi = juce::jmax (v, mv);
        for (int i = 0; i < N; ++i)
        {
            const float f = (float) i / (float) (N - 1);
            const float ang = a0 - f * span;
            const auto pc = sq.getCentre() + juce::Point<float> (std::cos (ang), -std::sin (ang)) * ringR;
            const bool on = f <= v + 1.0e-4f && v > 0.0f;
            const bool swing = live && f >= lo - 0.03f && f <= hi + 0.03f;
            juce::Colour col = on ? t.thermal (0.25f + 0.75f * f) : t.panelLo.withAlpha (0.8f);
            if (swing) col = t.accent2;
            g.setColour (col);
            g.fillRect (juce::Rectangle<float> (blk, blk).withCentre (pc).getSmallestIntegerContainer());
        }

        const float cr = ringR - blk - 2.0f;
        juce::Path oct;
        for (int i = 0; i < 8; ++i)
        {
            const float ang = juce::MathConstants<float>::twoPi * ((float) i + 0.5f) / 8.0f;
            const auto pt = sq.getCentre() + juce::Point<float> (std::cos (ang), std::sin (ang)) * cr;
            if (i == 0) oct.startNewSubPath (pt); else oct.lineTo (pt);
        }
        oct.closeSubPath();
        g.setColour (juce::Colours::black.withAlpha (0.4f));
        g.fillPath (oct, juce::AffineTransform::translation (2.0f, 2.0f));
        g.setGradientFill (juce::ColourGradient (t.panelHi, sq.getCentre() - juce::Point<float> (cr, cr),
                                                 t.panelLo, sq.getCentre() + juce::Point<float> (cr, cr), false));
        g.fillPath (oct);
        g.setColour (t.panel);
        g.fillEllipse (juce::Rectangle<float> (cr * 1.25f, cr * 1.25f).withCentre (sq.getCentre()));
        g.setColour (t.panelLo.darker (0.5f));
        g.strokePath (oct, juce::PathStrokeType (1.0f));

        auto pointer = [&] (float val, juce::Colour col, float w)
        {
            const float ang = a0 - val * span;
            const auto tip = sq.getCentre() + juce::Point<float> (std::cos (ang), -std::sin (ang)) * (cr - 2.0f);
            const auto base = sq.getCentre() + juce::Point<float> (std::cos (ang), -std::sin (ang)) * (cr * 0.25f);
            g.setColour (col);
            g.drawLine ({ base, tip }, w);
        };
        if (live) pointer (v, t.textDim.withAlpha (0.7f), 1.5f);
        pointer (mv, live ? t.accent2 : (isMouseOverOrDragging() ? t.accent2 : t.accent), 2.5f);

        draw::screenWell (g, read, t);
        g.setColour (live ? t.accent2 : t.ink);
        g.setFont (screenFont (small ? 15.0f : 16.0f));
        auto txt = live ? "~" + param.getText (mv, 0) : param.getCurrentValueAsText();
        if (juce::GlyphArrangement::getStringWidthInt (g.getCurrentFont(), txt) > read.getWidth() - 2)
            txt = txt.upToFirstOccurrenceOf (" ", false, false);
        g.drawText (txt, read, juce::Justification::centred);
    }

    void mouseEnter (const juce::MouseEvent&) override { if (host != nullptr && getTooltip().isNotEmpty()) host->hint (getTooltip()); }
    void mouseDown (const juce::MouseEvent& e) override
    {
        if (e.mods.isPopupMenu()) { draw::contextMenu (host, paramId(), this); return; }
        attach.beginGesture();
        dragStart = param.getValue();
        dragging = true;
    }
    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (! dragging) return;
        const float sens = e.mods.isShiftDown() ? 1200.0f : 220.0f;
        const float nv = juce::jlimit (0.0f, 1.0f, dragStart - (float) e.getDistanceFromDragStartY() / sens);
        attach.setValueAsPartOfGesture (param.convertFrom0to1 (nv));
    }
    void mouseUp (const juce::MouseEvent&) override
    {
        if (dragging) attach.endGesture();
        dragging = false;
    }
    void mouseDoubleClick (const juce::MouseEvent& e) override
    {
        if (e.mods.isPopupMenu()) return;
        attach.setValueAsCompleteGesture (param.convertFrom0to1 (param.getDefaultValue()));
    }
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails& w) override
    {
        const float nv = juce::jlimit (0.0f, 1.0f, param.getValue() + w.deltaY * 0.05f);
        attach.setValueAsCompleteGesture (param.convertFrom0to1 (nv));
    }

    // ---- LFO tiles are dropped here
    bool isInterestedInDragSource (const SourceDetails& d) override
    {
        return host != nullptr && host->canModulate (paramId()) && d.description.toString().startsWith ("lfo:");
    }
    void itemDragEnter (const SourceDetails&) override { dropHover = true; repaint(); }
    void itemDragExit (const SourceDetails&) override { dropHover = false; repaint(); }
    void itemDropped (const SourceDetails& d) override
    {
        dropHover = false;
        host->addMod (d.description.toString().fromFirstOccurrenceOf ("lfo:", false, false).getIntValue(), paramId());
        repaint();
    }

private:
    Skin& skin;
    juce::RangedAudioParameter& param;
    juce::String label;
    Host* host;
    bool small;
    juce::ParameterAttachment attach;
    float dragStart = 0.0f;
    bool dragging = false, dropHover = false;
};

//==============================================================================
// A pop-up cell bound to a choice / int / bool parameter.
class MenuCell : public juce::Component, public juce::SettableTooltipClient
{
public:
    MenuCell (Skin& s, juce::RangedAudioParameter& p, juce::String lbl = {}, Host* h = nullptr)
        : skin (s), param (p), label (std::move (lbl)), host (h),
          attach (p, [this] (float) { repaint(); }, nullptr)
    {
        attach.sendInitialUpdate();
        setRepaintsOnMouseActivity (true);
    }

    void paint (juce::Graphics& g) override
    {
        const auto& t = skin.t();
        auto r = getLocalBounds();
        if (label.isNotEmpty())
            draw::smallLabel (g, label, r.removeFromTop (12), t);

        g.setColour (t.panelLo.darker (0.5f));
        g.drawRect (r);
        auto in = r.reduced (1);
        g.setColour (isMouseOver() ? t.panel.brighter (0.08f) : t.panel);
        g.fillRect (in);
        draw::bevel (g, in, t, true, 1);

        auto arrow = in.removeFromRight (13).reduced (2);
        g.setColour (t.panelLo);
        g.fillRect (arrow.withWidth (1).translated (-2, 0));
        juce::Path tri;
        const auto cpt = arrow.getCentre().toFloat();
        tri.addTriangle (cpt.x - 3.5f, cpt.y - 2.0f, cpt.x + 3.5f, cpt.y - 2.0f, cpt.x, cpt.y + 2.5f);
        g.setColour (t.text);
        g.fillPath (tri);
        if (host != nullptr && host->isLocked (param.getParameterID()))
            draw::padlock (g, in.removeFromRight (10).withSizeKeepingCentre (8, 8), t.accent);

        g.setColour (dimmed && dimmed() ? t.textDim : t.text);
        g.setFont (screenFont (fontSize));
        g.drawText (param.getCurrentValueAsText(), in.reduced (4, 0), juce::Justification::centredLeft);
    }

    float fontSize = 18.0f;                 // smaller in the tight rows (X-MOD, LOGIC)
    std::function<bool()> dimmed;           // drawn dim while this says it does nothing (X-MOD off)
    // a menu of its own (submenus, sections) instead of the flat list; it calls choose()
    std::function<void (MenuCell&)> customMenu;
    void choose (int value) { attach.setValueAsCompleteGesture ((float) value); }
    int current() const { return juce::roundToInt (param.convertFrom0to1 (param.getValue())); }
    juce::RangedAudioParameter& parameter() { return param; }

    void mouseEnter (const juce::MouseEvent&) override { if (host != nullptr && getTooltip().isNotEmpty()) host->hint (getTooltip()); }
    void mouseDown (const juce::MouseEvent& e) override
    {
        if (e.mods.isPopupMenu()) { draw::contextMenu (host, param.getParameterID(), this); return; }
        if (customMenu) { customMenu (*this); return; }
        juce::PopupMenu m;
        const auto& range = param.getNormalisableRange();
        const int lo = (int) range.start, hi = (int) range.end;
        const int cur = juce::roundToInt (param.convertFrom0to1 (param.getValue()));
        for (int v = lo; v <= hi; ++v)
            m.addItem (v - lo + 1, param.getText (param.convertTo0to1 ((float) v), 0), true, cur == v);
        juce::Component::SafePointer<MenuCell> safe (this);
        m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this).withMinimumWidth (getWidth()),
                         [safe, lo] (int res)
                         {
                             if (safe == nullptr || res <= 0) return;
                             safe->attach.setValueAsCompleteGesture ((float) (res - 1 + lo));
                         });
    }
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails& w) override
    {
        const auto& range = param.getNormalisableRange();
        const int cur = juce::roundToInt (param.convertFrom0to1 (param.getValue()));
        const int nv = juce::jlimit ((int) range.start, (int) range.end, cur + (w.deltaY > 0 ? 1 : -1));
        attach.setValueAsCompleteGesture ((float) nv);
    }

private:
    Skin& skin;
    juce::RangedAudioParameter& param;
    juce::String label;
    Host* host;
    juce::ParameterAttachment attach;
};

//==============================================================================
// Latching push button with an LED, bound to a bool parameter.
class Toggle : public juce::Component, public juce::SettableTooltipClient
{
public:
    Toggle (Skin& s, juce::RangedAudioParameter& p, juce::String txt, juce::String offTxt = {}, Host* h = nullptr)
        : skin (s), param (p), text (std::move (txt)), offText (std::move (offTxt)), host (h),
          attach (p, [this] (float) { repaint(); }, nullptr)
    {
        attach.sendInitialUpdate();
        setRepaintsOnMouseActivity (true);
    }

    bool on() const { return param.getValue() > 0.5f; }

    void paint (juce::Graphics& g) override
    {
        const auto& t = skin.t();
        auto r = getLocalBounds();
        g.setColour (t.panelLo.darker (0.5f));
        g.drawRect (r);
        auto in = r.reduced (1);
        g.setColour (on() ? t.panel.darker (0.12f) : (isMouseOver() ? t.panel.brighter (0.06f) : t.panel));
        g.fillRect (in);
        draw::bevel (g, in, t, ! on(), 2);

        auto led = in.removeFromLeft (16).withSizeKeepingCentre (7, 7).translated (on() ? 3 : 2, on() ? 1 : 0);
        g.setColour (t.panelLo.darker (0.4f));
        g.drawRect (led.expanded (1));
        g.setColour (on() ? t.accent : t.screen);
        g.fillRect (led);
        if (on()) { g.setColour (t.accent.withAlpha (0.25f)); g.fillRect (led.expanded (3)); }
        if (host != nullptr && host->isLocked (param.getParameterID()))
            draw::padlock (g, in.removeFromRight (11).withSizeKeepingCentre (8, 8), t.accent);

        g.setColour (on() ? t.text : t.textDim);
        g.setFont (labelFont (8.5f));
        const auto s = (! on() && offText.isNotEmpty()) ? offText : text;
        g.drawText (s, in.translated (on() ? 1 : 0, on() ? 1 : 0).withTrimmedRight (2), juce::Justification::centredLeft);
    }

    void mouseEnter (const juce::MouseEvent&) override { if (host != nullptr && getTooltip().isNotEmpty()) host->hint (getTooltip()); }
    void mouseDown (const juce::MouseEvent& e) override
    {
        if (e.mods.isPopupMenu()) { draw::contextMenu (host, param.getParameterID(), this); return; }
        attach.setValueAsCompleteGesture (on() ? param.convertFrom0to1 (0.0f) : param.convertFrom0to1 (1.0f));
    }

private:
    Skin& skin;
    juce::RangedAudioParameter& param;
    juce::String text, offText;
    Host* host;
    juce::ParameterAttachment attach;
};

//==============================================================================
// A number in a screen well, bound to an int / float parameter: drag up and down, wheel,
// double-click for the default. For the tight rows (tempo, start bar, CC numbers).
class NumberBox : public juce::Component, public juce::SettableTooltipClient
{
public:
    NumberBox (Skin& s, juce::RangedAudioParameter& p, juce::String lbl = {}, Host* h = nullptr)
        : skin (s), param (p), label (std::move (lbl)), host (h), attach (p, [this] (float) { repaint(); }, nullptr)
    {
        attach.sendInitialUpdate();
        setRepaintsOnMouseActivity (true);
        setMouseCursor (juce::MouseCursor::UpDownResizeCursor);
    }
    float fontSize = 18.0f;
    juce::String prefix;

    void paint (juce::Graphics& g) override
    {
        const auto& t = skin.t();
        auto r = getLocalBounds();
        if (label.isNotEmpty()) draw::smallLabel (g, label, r.removeFromTop (12), t);
        draw::screenWell (g, r.reduced (2, 2), t);
        g.setColour (isMouseOverOrDragging() ? t.accent2 : t.ink);
        g.setFont (screenFont (fontSize));
        g.drawText (prefix + param.getCurrentValueAsText(), r.reduced (4, 0), juce::Justification::centred);
    }
    void mouseEnter (const juce::MouseEvent&) override { if (host != nullptr && getTooltip().isNotEmpty()) host->hint (getTooltip()); }
    void mouseDown (const juce::MouseEvent& e) override
    {
        if (e.mods.isPopupMenu()) return;
        attach.beginGesture();
        start = param.convertFrom0to1 (param.getValue());
    }
    void mouseDrag (const juce::MouseEvent& e) override
    {
        const auto& range = param.getNormalisableRange();
        const float span = range.end - range.start;
        const float per = e.mods.isShiftDown() ? 0.25f : 1.0f;
        float steps = (float) -e.getDistanceFromDragStartY() / 4.0f * per;
        if (range.interval <= 0.0f && span <= 4.0f) steps *= span / 200.0f;
        attach.setValueAsPartOfGesture (juce::jlimit (range.start, range.end, start + steps));
    }
    void mouseUp (const juce::MouseEvent&) override { attach.endGesture(); }
    void mouseDoubleClick (const juce::MouseEvent&) override { attach.setValueAsCompleteGesture (param.convertFrom0to1 (param.getDefaultValue())); }
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails& w) override
    {
        const auto& range = param.getNormalisableRange();
        const float cur = param.convertFrom0to1 (param.getValue());
        const float d = range.end - range.start > 4.0f ? 1.0f : (range.end - range.start) / 50.0f;
        attach.setValueAsCompleteGesture (juce::jlimit (range.start, range.end, cur + (w.deltaY > 0 ? d : -d)));
    }

private:
    Skin& skin;
    juce::RangedAudioParameter& param;
    juce::String label;
    Host* host;
    juce::ParameterAttachment attach;
    float start = 0.0f;
};

//==============================================================================
class Scope : public juce::Component
{
public:
    Scope (Skin& s, const float* l, const float* r, std::atomic<int>& w, int len)
        : skin (s), bufL (l), bufR (r), write (w), length (len) {}

    void paint (juce::Graphics& g) override
    {
        const auto& t = skin.t();
        auto r = getLocalBounds();
        draw::screenWell (g, r, t);
        auto in = r.reduced (3);
        auto top = in.removeFromTop (in.getHeight() / 2);
        trace (g, top, bufL, t, "L");
        trace (g, in, bufR, t, "R");
        draw::scanlines (g, r.reduced (3), t, 0.15f);
    }

private:
    void trace (juce::Graphics& g, juce::Rectangle<int> area, const float* buf, const Theme& t, const char* name)
    {
        const float mid = (float) area.getCentreY(), h = (float) area.getHeight() * 0.45f;
        g.setColour (t.inkDim);
        g.fillRect ((float) area.getX(), mid, (float) area.getWidth(), 1.0f);
        const int w = write.load (std::memory_order_acquire);
        const int n = juce::jmin (length, area.getWidth() * 2);
        juce::Path p;
        for (int i = 0; i < n; ++i)
        {
            const float v = juce::jlimit (-1.0f, 1.0f, buf[(size_t) ((w - n + i + length) % length)]);
            const float x = (float) area.getX() + (float) area.getWidth() * (float) i / (float) n;
            if (i == 0) p.startNewSubPath (x, mid - v * h); else p.lineTo (x, mid - v * h);
        }
        g.setColour (t.ink.withAlpha (0.25f));
        g.strokePath (p, juce::PathStrokeType (3.0f));
        g.setColour (t.ink);
        g.strokePath (p, juce::PathStrokeType (1.0f));
        g.setFont (screenFont (14.0f));
        g.setColour (t.screen.getBrightness() > 0.5f ? t.ink.withAlpha (0.7f) : t.inkDim.brighter (0.4f));
        g.drawText (name, area.reduced (3, 0), juce::Justification::topLeft);
    }

    Skin& skin;
    const float* bufL;
    const float* bufR;
    std::atomic<int>& write;
    int length;
};

} // namespace ui
