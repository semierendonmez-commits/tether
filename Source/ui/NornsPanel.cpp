#include "NornsPanel.h"
#include "../PluginProcessor.h"

namespace ui
{

//==============================================================================
// An endless encoder: knurled cap, a notch that turns with every step sent.
class NornsPanel::Encoder : public juce::Component, public juce::SettableTooltipClient
{
public:
    Encoder (Skin& s, TetherProcessor& p, int index) : skin (s), proc (p), n (index)
    {
        setRepaintsOnMouseActivity (true);
        setMouseCursor (juce::MouseCursor::UpDownResizeCursor);
        setTooltip ("E" + juce::String (n) + ": drag up / down or use the wheel (shift: fine).");
    }

    float flash = 0.0f, angle = 0.0f;

    void turn (int d)
    {
        if (d == 0) return;
        proc.norns.enc (n, d);
        angle += 0.22f * (float) d;
        repaint();
    }

    void paint (juce::Graphics& g) override
    {
        const auto& t = skin.t();
        auto r = getLocalBounds();
        draw::smallLabel (g, "E" + juce::String (n), r.removeFromBottom (12), t, juce::Justification::centred);
        const float d = (float) juce::jmin (r.getWidth(), r.getHeight()) - 4.0f;
        auto c = juce::Rectangle<float> (d, d).withCentre (r.getCentre().toFloat());

        // the collar
        g.setColour (juce::Colours::black.withAlpha (0.45f));
        g.fillEllipse (c.translated (2.5f, 3.0f));
        g.setColour (t.panelLo.darker (0.4f));
        g.fillEllipse (c);
        if (flash > 0.0f)
        {
            g.setColour (t.accent2.withAlpha (0.8f * flash));
            g.drawEllipse (c.expanded (2.0f), 2.5f);
        }
        // the knurl: alternating teeth around the edge
        auto cap = c.reduced (3.0f);
        const auto ctr = cap.getCentre();
        const float R = cap.getWidth() * 0.5f;
        for (int k = 0; k < 36; ++k)
        {
            const float a = angle + juce::MathConstants<float>::twoPi * (float) k / 36.0f;
            juce::Path tooth;
            tooth.addCentredArc (ctr.x, ctr.y, R, R, 0.0f, a, a + juce::MathConstants<float>::twoPi / 72.0f, true);
            tooth.lineTo (ctr);
            tooth.closeSubPath();
            g.setColour ((k & 1) ? t.panelHi : t.panel);
            g.fillPath (tooth);
        }
        auto top = cap.reduced (R * 0.18f);
        g.setGradientFill (juce::ColourGradient (t.panelHi, top.getTopLeft(), t.panelLo, top.getBottomRight(), false));
        g.fillEllipse (top);
        g.setColour (t.panelLo.darker (0.5f));
        g.drawEllipse (top, 1.0f);
        // the notch
        const float a = angle - juce::MathConstants<float>::halfPi;
        const auto tip = ctr + juce::Point<float> (std::cos (a), std::sin (a)) * (top.getWidth() * 0.42f);
        const auto base = ctr + juce::Point<float> (std::cos (a), std::sin (a)) * (top.getWidth() * 0.12f);
        g.setColour (isMouseOverOrDragging() ? t.accent2 : t.accent);
        g.drawLine ({ base, tip }, 3.0f);
    }

    void mouseDown (const juce::MouseEvent&) override { acc = 0.0f; lastY = 0; if (auto* p = getParentComponent()) p->grabKeyboardFocus(); }
    void mouseDrag (const juce::MouseEvent& e) override
    {
        const int y = e.getDistanceFromDragStartY();
        acc += (float) (lastY - y) / (e.mods.isShiftDown() ? 14.0f : 5.0f);
        lastY = y;
        const int steps = (int) acc;
        if (steps != 0) { acc -= (float) steps; turn (steps); }
    }
    void mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w) override
    {
        wheel += w.deltaY * (e.mods.isShiftDown() ? 6.0f : 24.0f);
        const int steps = (int) wheel;
        if (steps != 0) { wheel -= (float) steps; turn (steps); }
    }

private:
    Skin& skin;
    TetherProcessor& proc;
    const int n;
    float acc = 0.0f, wheel = 0.0f;
    int lastY = 0;
};

//==============================================================================
// A key: held while the mouse is down, just as on the norns.
class NornsPanel::Key : public juce::Component, public juce::SettableTooltipClient
{
public:
    Key (Skin& s, TetherProcessor& p, int index) : skin (s), proc (p), n (index)
    {
        setRepaintsOnMouseActivity (true);
        setTooltip ("K" + juce::String (n) + ": click and hold (or key " + juce::String (n) + " with the panel focused).");
    }

    float flash = 0.0f;
    bool down = false;

    void press (bool on)
    {
        if (on == down) return;
        down = on;
        proc.norns.key (n, on ? 1 : 0);
        repaint();
    }

    void paint (juce::Graphics& g) override
    {
        const auto& t = skin.t();
        auto r = getLocalBounds();
        draw::smallLabel (g, "K" + juce::String (n), r.removeFromBottom (12), t, juce::Justification::centred);
        const float d = (float) juce::jmin (r.getWidth(), r.getHeight()) - 6.0f;
        auto c = juce::Rectangle<float> (d, d).withCentre (r.getCentre().toFloat());
        g.setColour (juce::Colours::black.withAlpha (0.45f));
        g.fillEllipse (c.translated (2.0f, 2.5f));
        g.setColour (t.panelLo.darker (0.4f));
        g.fillEllipse (c);
        auto cap = c.reduced (down ? 4.0f : 3.0f).translated (down ? 1.0f : 0.0f, down ? 1.0f : 0.0f);
        const auto lit = down || flash > 0.0f;
        g.setGradientFill (juce::ColourGradient (lit ? t.accent.brighter (0.3f) : t.panelHi, cap.getTopLeft(),
                                                 lit ? t.accent.darker (0.4f) : t.panelLo, cap.getBottomRight(), false));
        g.fillEllipse (cap);
        if (flash > 0.0f)
        {
            g.setColour (t.accent2.withAlpha (0.8f * flash));
            g.drawEllipse (c.expanded (2.0f), 2.5f);
        }
        g.setColour (t.panelLo.darker (0.5f));
        g.drawEllipse (cap, 1.0f);
    }

    void mouseDown (const juce::MouseEvent&) override { press (true); if (auto* p = getParentComponent()) p->grabKeyboardFocus(); }
    void mouseUp (const juce::MouseEvent&) override { press (false); }

private:
    Skin& skin;
    TetherProcessor& proc;
    const int n;
};

//==============================================================================
NornsPanel::NornsPanel (Skin& s, TetherProcessor& p) : skin (s), proc (p)
{
    setWantsKeyboardFocus (true);
    for (int i = 0; i < 3; ++i)
    {
        enc[i] = std::make_unique<Encoder> (skin, proc, i + 1);
        key[i] = std::make_unique<Key> (skin, proc, i + 1);
        addAndMakeVisible (*enc[i]);
        addAndMakeVisible (*key[i]);
    }
    img = juce::Image (juce::Image::RGB, tether::kScreenW * 4, tether::kScreenH * 4, true);

    for (auto* b : { &styleBtn, &recBtn, &snapBtn }) addAndMakeVisible (b);
    styleBtn.text = "SCREEN";
    styleBtn.setTooltip ("SCREEN: how the norns screen is coloured here, in snapshots and in recordings.");
    styleBtn.onClick = [this] { styleMenu(); };
    recBtn.text = "REC";
    recBtn.setTooltip ("REC: record the norns screen to a GIF in ~/Music/TETHER (click again to stop). Drag this button out to drop the last recording somewhere.");
    recBtn.onClick = [this]
    {
        if (proc.isRecording()) proc.stopRecording(); else proc.startRecording();
        recBtn.repaint();
    };
    recBtn.onDragOut = [this]
    {
        const auto f = proc.lastRecording();
        if (f.existsAsFile() && ! proc.encoding()) juce::DragAndDropContainer::performExternalDragDropOfFiles ({ f.getFullPathName() }, false, this);
    };
    snapBtn.text = "SNAP";
    snapBtn.setTooltip ("SNAP: save the screen as it is now, a PNG in ~/Music/TETHER (drag this button out to drop the last one).");
    snapBtn.onClick = [this] { lastSnap = proc.snapshot(); };
    snapBtn.onDragOut = [this]
    {
        if (lastSnap.existsAsFile()) juce::DragAndDropContainer::performExternalDragDropOfFiles ({ lastSnap.getFullPathName() }, false, this);
    };
}

void NornsPanel::styleMenu()
{
    juce::PopupMenu m;
    m.addSectionHeader ("SCREEN COLOURS");
    const int cur = proc.getScreenStyle();
    for (int i = 0; i < kNumScreenStyles; ++i)
        m.addItem (i + 1, juce::String (screenStyleName (i)) + "   " + screenStyleHint (i), true, i == cur);
    juce::Component::SafePointer<NornsPanel> safe (this);
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&styleBtn), [safe] (int r)
    {
        if (safe == nullptr || r <= 0) return;
        safe->proc.setScreenStyle (r - 1);
        safe->lastStyle = -1;
    });
}

NornsPanel::~NornsPanel()
{
    for (int i = 0; i < 3; ++i) if (key[i]->down) key[i]->press (false);   // never leave a key held
}

void NornsPanel::resized()
{
    auto r = getLocalBounds();
    body = r;
    const int sw = tether::kScreenW * 4, sh = tether::kScreenH * 4;
    screenRect = juce::Rectangle<int> (sw, sh).withCentre ({ r.getCentreX(), r.getY() + 14 + sh / 2 });

    // under the screen, as on the norns: K1 E1 on the left, E2 E3 then K2 K3 on the right
    const int y = screenRect.getBottom() + 14;
    const int encS = 74, keyS = 50;
    enc[0]->setBounds (screenRect.getX() + 62, y, encS, encS + 12);
    key[0]->setBounds (screenRect.getX(), y + 12, keyS, keyS + 12);
    enc[2]->setBounds (screenRect.getRight() - encS, y, encS, encS + 12);
    enc[1]->setBounds (enc[2]->getX() - encS - 16, y, encS, encS + 12);
    key[1]->setBounds (enc[1]->getX() - 118, y + 12, keyS, keyS + 12);
    key[2]->setBounds (key[1]->getRight() + 8, y + 12, keyS, keyS + 12);

    // between E1 and K2: the screen's own buttons
    const int gx = enc[0]->getRight() + 8, gw = key[1]->getX() - gx - 8;
    styleBtn.setBounds (gx, y + 8, gw, 28);
    recBtn.setBounds (gx, y + 42, gw / 2 - 2, 28);
    snapBtn.setBounds (gx + gw / 2 + 2, y + 42, gw - gw / 2 - 2, 28);
}

void NornsPanel::rebuildImage()
{
    pal = screenPalette (skin.t(), proc.getScreenStyle());
    juce::Colour edge[16];
    for (int k = 0; k < 16; ++k) edge[k] = k == 0 ? pal.grid : pal.level[k].interpolatedWith (pal.bg, 0.45f);
    juce::Image::BitmapData bd (img, juce::Image::BitmapData::writeOnly);
    for (int y = 0; y < tether::kScreenH; ++y)
        for (int x = 0; x < tether::kScreenW; ++x)
        {
            const int lv = pix[(size_t) (y * tether::kScreenW + x)] & 15;
            const auto c = pal.level[lv], e = edge[lv];
            for (int dy = 0; dy < 4; ++dy)
                for (int dx = 0; dx < 4; ++dx)
                    bd.setPixelColour (x * 4 + dx, y * 4 + dy, (dx == 3 || dy == 3) ? e : c);
        }
}

void NornsPanel::pump (float dt)
{
    if (proc.norns.readScreen (pix.data(), serial)) { ++frames; rebuildImage(); repaint (screenRect.expanded (4)); }
    if (skin.index != lastTheme || proc.getScreenStyle() != lastStyle)
    {
        lastTheme = skin.index;
        lastStyle = proc.getScreenStyle();
        rebuildImage();
        styleBtn.text = screenStyleName (lastStyle);
        styleBtn.repaint();
        repaint();
    }

    // REC: the elapsed time on the button while it runs, a blinking dot over the screen
    const bool rec = proc.isRecording();
    const juce::String rt = rec ? juce::String ((int) proc.recordingSeconds() / 60) + ":" + juce::String ((int) proc.recordingSeconds() % 60).paddedLeft ('0', 2)
                                : juce::String (proc.encoding() ? "SAVING" : "REC");
    if (rt != recBtn.text) { recBtn.text = rt; recBtn.repaint(); }
    if (rec) { blink += dt; repaint (screenRect.getRight() - 70, screenRect.getY() - 14, 76, 14); }

    for (int i = 0; i < 6; ++i)
    {
        const auto c = proc.norns.hwCount (i);
        if (c != hwSeen[(size_t) i])
        {
            hwSeen[(size_t) i] = c;
            if (i < 3) key[i]->flash = 1.0f; else { enc[i - 3]->flash = 1.0f; enc[i - 3]->angle += 0.22f; }
        }
    }
    for (int i = 0; i < 3; ++i)
    {
        if (key[i]->flash > 0.0f) { key[i]->flash = juce::jmax (0.0f, key[i]->flash - dt * 3.0f); key[i]->repaint(); }
        if (enc[i]->flash > 0.0f) { enc[i]->flash = juce::jmax (0.0f, enc[i]->flash - dt * 3.0f); enc[i]->repaint(); }
    }
    // without a mirror the well shows the link's state: keep it current
    if (frames == 0) repaint (screenRect);
}

void NornsPanel::paint (juce::Graphics& g)
{
    const auto& t = skin.t();
    // the faceplate
    auto plate = body.reduced (2);
    g.setColour (t.panel.darker (0.08f));
    g.fillRect (plate);
    draw::bevel (g, plate, t, true, 1);
    for (const auto& corner : { plate.getTopLeft(), plate.getTopRight().translated (-9, 0), plate.getBottomLeft().translated (0, -9),
                                plate.getBottomRight().translated (-9, -9) })
    {
        auto sc = juce::Rectangle<float> ((float) corner.x + 3.0f, (float) corner.y + 3.0f, 6.0f, 6.0f);
        g.setColour (t.panelLo);
        g.fillEllipse (sc);
        g.setColour (t.panelHi);
        g.drawLine (sc.getX() + 1.0f, sc.getBottom() - 1.0f, sc.getRight() - 1.0f, sc.getY() + 1.0f, 1.0f);
    }

    draw::screenWell (g, screenRect, t);
    g.setColour (pal.bg);
    g.fillRect (screenRect);
    const bool live = frames > 0 && proc.norns.status() == tether::Status::Connected;
    if (frames > 0)
    {
        juce::Graphics::ScopedSaveState ss (g);
        g.setImageResamplingQuality (juce::Graphics::lowResamplingQuality);
        g.setOpacity (live ? 1.0f : 0.35f);
        g.drawImageAt (img, screenRect.getX(), screenRect.getY());
    }
    if (! live)
    {
        const auto st = proc.norns.status();
        juce::String a = tether::statusName (st), b;
        switch (st)
        {
            case tether::Status::Off:       b = "LINK is off"; break;
            case tether::Status::Resolving: b = "looking for " + proc.getHost(); break;
            case tether::Status::NotFound:  b = proc.getHost() + " did not answer the name lookup"; break;
            case tether::Status::Waiting:   b = "hello sent to " + proc.getHost(); break;
            case tether::Status::NoMod:     b = "is the tether mod on? keys and encoders still go out"; break;
            case tether::Status::Lost:      b = "the norns went quiet"; break;
            case tether::Status::Connected: b = "waiting for the first frame"; break;
        }
        g.setColour (pal.text);
        g.setFont (screenFont (34.0f));
        g.drawText (a, screenRect.withTrimmedBottom (40), juce::Justification::centred);
        g.setColour (pal.text.withAlpha (0.7f));
        g.setFont (screenFont (19.0f));
        g.drawText (b, screenRect.withTrimmedTop (70).reduced (12, 0), juce::Justification::centred);
    }
    draw::scanlines (g, screenRect, t, pal.bg.getPerceivedBrightness() > 0.5f ? 0.04f : 0.10f);

    if (proc.isRecording())
    {
        auto r = juce::Rectangle<int> (screenRect.getRight() - 64, screenRect.getY() - 13, 64, 12);
        const bool on = std::fmod (blink, 1.0f) < 0.6f;
        g.setColour (juce::Colour (0xffe8302a).withAlpha (on ? 1.0f : 0.25f));
        g.fillEllipse (r.removeFromLeft (12).reduced (2).toFloat());
        g.setColour (t.text);
        g.setFont (labelFont (8.0f));
        g.drawText ("RECORDING", r.withTrimmedLeft (3), juce::Justification::centredLeft);
    }

    if (hasKeyboardFocus (true))
    {
        g.setColour (t.accent2.withAlpha (0.6f));
        g.drawRect (screenRect.expanded (5), 1);
    }
}

bool NornsPanel::keyPressed (const juce::KeyPress& k)
{
    const auto c = juce::CharacterFunctions::toLowerCase (k.getTextCharacter());
    if (c >= '1' && c <= '3')
    {
        const int n = c - '0';
        if (! held[n]) { held[n] = true; key[n - 1]->press (true); }
        return true;
    }
    const int sign = k.getModifiers().isShiftDown() ? 1 : 4;
    switch (c)
    {
        case 'q': enc[0]->turn (sign); return true;
        case 'a': enc[0]->turn (-sign); return true;
        case 'w': enc[1]->turn (sign); return true;
        case 's': enc[1]->turn (-sign); return true;
        case 'e': enc[2]->turn (sign); return true;
        case 'd': enc[2]->turn (-sign); return true;
        default: break;
    }
    return false;
}

bool NornsPanel::keyStateChanged (bool)
{
    bool used = false;
    for (int n = 1; n <= 3; ++n)
        if (held[n] && ! juce::KeyPress::isKeyCurrentlyDown ('0' + n))
        {
            held[n] = false;
            key[n - 1]->press (false);
            used = true;
        }
    return used;
}

} // namespace ui
