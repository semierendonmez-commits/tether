#include "PluginEditor.h"
#include "ui/Ink.h"

using namespace tether;

namespace
{
    juce::Rectangle<int> contentOf (juce::Rectangle<int> win) { return win.reduced (4).withTrimmedTop (19); }

    template <typename T, typename... A>
    std::unique_ptr<T> make (juce::Component& parent, const juce::String& tip, A&&... a)
    {
        auto c = std::make_unique<T> (std::forward<A> (a)...);
        c->setTooltip (tip);
        parent.addAndMakeVisible (*c);
        return c;
    }
}

//==============================================================================
// One slot: what it is bound to, where it is (knob + CONDUIT), drag to move it.
class TetherEditor::SlotCell : public juce::Component, public juce::SettableTooltipClient
{
public:
    SlotCell (ui::Skin& s, TetherProcessor& p, ui::Host* h, int index)
        : skin (s), proc (p), host (h), n (index),
          attach (p.slotParam (index), [this] (float) { repaint(); }, nullptr)
    {
        attach.sendInitialUpdate();
        setRepaintsOnMouseActivity (true);
        setTooltip ("SLOT " + juce::String (n + 1) + ": a host parameter (automate it) bound to a norns parameter. "
                    "Click the name to bind; drag the bar to move it."
                    + (n < 16 ? " On the family bus as SLOT" + juce::String (n + 1) + " (CONDUIT can patch into it)." : juce::String()));
    }

    float shownValue = -1.0f;
    bool shownMod = false;

    void paint (juce::Graphics& g) override
    {
        const auto& t = skin.t();
        auto r = getLocalBounds();
        const bool bound = proc.slotId (n).isNotEmpty();
        const bool mod = proc.slotModulated (n);
        g.setColour (t.panelLo.darker (0.5f));
        g.drawRect (r);
        auto in = r.reduced (1);
        g.setColour (isMouseOver() ? t.panel.brighter (0.06f) : t.panel);
        g.fillRect (in);
        ui::draw::bevel (g, in, t, true, 1);
        in = in.reduced (4, 3);

        auto top = in.removeFromTop (14);
        g.setFont (ui::labelFont (8.0f));
        g.setColour (bound ? t.text : t.textDim);
        g.drawText ("S" + juce::String (n + 1), top, juce::Justification::centredLeft);
        if (n < 16)
        {
            g.setColour (t.accent2.withAlpha (0.8f));
            g.fillRect (top.getRight() - 6, top.getCentreY() - 2, 5, 5);
        }
        if (mod)
        {
            auto badge = juce::Rectangle<int> (top.getRight() - 30, top.getY() + 1, 20, 11);
            g.setColour (t.accent2);
            g.fillRect (badge);
            g.setColour (t.screen);
            g.setFont (ui::screenFont (12.0f));
            g.drawText ("CV", badge, juce::Justification::centred);
        }

        auto well = in;
        ui::draw::screenWell (g, well, t);
        auto nameArea = well.removeFromTop (18).reduced (4, 0);
        juce::String valueText;
        if (bound)
        {
            tether::ParamInfo p;
            valueText = proc.norns.findParam (proc.slotId (n).toStdString(), p) ? juce::String (p.text) : juce::String ("--");
        }
        g.setFont (ui::screenFont (16.0f));
        g.setColour (t.ink.withAlpha (0.8f));
        const int vw = juce::jmin (nameArea.getWidth() / 2, juce::GlyphArrangement::getStringWidthInt (ui::screenFont (16.0f), valueText) + 4);
        g.drawText (valueText, nameArea.removeFromRight (vw), juce::Justification::centredRight, true);
        g.setFont (ui::screenFont (18.0f));
        g.setColour (bound ? t.ink : t.inkDim.brighter (0.3f));
        g.drawText (bound ? proc.slotParam (n).getName (40).fromFirstOccurrenceOf (" ", false, false) : juce::String ("bind..."),
                    nameArea, juce::Justification::centredLeft, true);

        auto barArea = well.reduced (3, 3);
        const float knob = proc.slotParam (n).getValue();
        const float out = proc.live.slot[(size_t) n].load();
        g.setColour (t.panelLo.withAlpha (0.5f));
        g.drawRect (barArea);
        auto b = barArea.reduced (1);
        g.setColour ((bound ? t.thermal (0.3f + 0.6f * out) : t.inkDim).withAlpha (bound ? 0.9f : 0.5f));
        g.fillRect (b.withWidth (juce::roundToInt ((float) b.getWidth() * out)));
        if (mod)
        {
            const int kx = b.getX() + juce::roundToInt ((float) b.getWidth() * knob);
            g.setColour (t.accent2);
            g.fillRect (kx - 1, b.getY(), 2, b.getHeight());
        }
        shownValue = out;
        shownMod = mod;
    }

    void mouseEnter (const juce::MouseEvent&) override { if (host != nullptr) host->hint (getTooltip()); }
    void mouseDown (const juce::MouseEvent& e) override
    {
        const bool onName = e.y < 36;
        if (e.mods.isPopupMenu() || onName || proc.slotId (n).isEmpty()) { menu(); return; }
        attach.beginGesture();
        dragging = true;
        setFromX (e.x);
    }
    void mouseDrag (const juce::MouseEvent& e) override { if (dragging) setFromX (e.x); }
    void mouseUp (const juce::MouseEvent&) override { if (dragging) attach.endGesture(); dragging = false; }
    void mouseDoubleClick (const juce::MouseEvent&) override { menu(); }

    void menu()
    {
        const auto snap = proc.norns.snapshot (true);
        juce::PopupMenu m;
        m.addSectionHeader ("SLOT " + juce::String (n + 1) + (snap.script != "none" ? "  >  " + juce::String (snap.script).toUpperCase() : juce::String()));
        const auto cur = proc.slotId (n);
        if (cur.isNotEmpty()) m.addItem (1, "unbind (" + proc.slotName (n) + ")");
        std::vector<std::pair<juce::String, juce::String>> items;
        for (const auto& p : snap.params)
            if (p.known && p.type != tether::TTrigger) items.emplace_back (p.id, p.name);
        if (items.empty()) m.addItem (2, "no parameters from norns yet", false);
        // short lists inline, long ones in pages of 30 named by their first and last parameter
        const int per = 30;
        if (! items.empty()) m.addSeparator();
        for (int a = 0; a < (int) items.size(); a += per)
        {
            juce::PopupMenu page;
            const int b = juce::jmin ((int) items.size(), a + per);
            for (int i = a; i < b; ++i)
            {
                const int other = proc.slotOf (items[(size_t) i].first);
                const auto label = items[(size_t) i].second + (other >= 0 && other != n ? "   (S" + juce::String (other + 1) + ")" : juce::String());
                if (items.size() <= (size_t) per) m.addItem (100 + i, label, true, items[(size_t) i].first == cur);
                else page.addItem (100 + i, label, true, items[(size_t) i].first == cur);
            }
            if (items.size() > (size_t) per) m.addSubMenu (items[(size_t) a].second + "  ...  " + items[(size_t) b - 1].second, page);
        }
        juce::Component::SafePointer<SlotCell> safe (this);
        m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this),
                         [safe, items] (int r)
                         {
                             if (safe == nullptr || r == 0) return;
                             if (r == 1) safe->proc.unbindSlot (safe->n);
                             else if (r >= 100 && r - 100 < (int) items.size())
                             {
                                 const auto& it = items[(size_t) r - 100];
                                 const int other = safe->proc.slotOf (it.first);
                                 if (other >= 0 && other != safe->n) safe->proc.unbindSlot (other);   // one slot per parameter
                                 safe->proc.bindSlot (safe->n, it.first, it.second);
                             }
                             safe->repaint();
                         });
    }

private:
    void setFromX (int x)
    {
        const auto b = getLocalBounds().reduced (6, 4);
        attach.setValueAsPartOfGesture (juce::jlimit (0.0f, 1.0f, (float) (x - b.getX()) / (float) juce::jmax (1, b.getWidth())));
    }

    ui::Skin& skin;
    TetherProcessor& proc;
    ui::Host* host;
    const int n;
    juce::ParameterAttachment attach;
    bool dragging = false;
};

//==============================================================================
// NET | AUDIO, on the LINK window's title bar.
class TetherEditor::Tabs : public juce::Component
{
public:
    Tabs (ui::Skin& s, std::function<int()> g, std::function<void (int)> st) : skin (s), get (std::move (g)), set (std::move (st)) { setRepaintsOnMouseActivity (true); }
    void paint (juce::Graphics& g) override
    {
        const auto& t = skin.t();
        const char* names[] = { "NET", "AUDIO" };
        const int w = getWidth() / 2;
        for (int i = 0; i < 2; ++i)
        {
            auto r = juce::Rectangle<int> (i * w, 0, w - 2, getHeight());
            const bool on = get() == i;
            g.setColour (on ? t.panel : t.titleA);
            g.fillRect (r);
            g.setColour (t.panelLo.darker (0.4f));
            g.drawRect (r);
            g.setColour (on ? t.text : t.titleText.withAlpha (0.8f));
            g.setFont (ui::labelFont (8.0f));
            g.drawText (names[i], r, juce::Justification::centred);
        }
    }
    void mouseDown (const juce::MouseEvent& e) override { set (e.x < getWidth() / 2 ? 0 : 1); repaint(); }

private:
    ui::Skin& skin;
    std::function<int()> get;
    std::function<void (int)> set;
};

//==============================================================================
TetherEditor::TetherEditor (TetherProcessor& p) : AudioProcessorEditor (p), proc (p)
{
    skin.index = juce::jlimit (0, ui::numThemes() - 1, (int) proc.apvts.state.getProperty ("theme", 0));
    setLookAndFeel (&look);
    look.refreshColours();

    addAndMakeVisible (header);
    header.statusRight = 230;
    addAndMakeVisible (nornsPanel);
    addAndMakeVisible (paramList);
    paramList.onHint = [this] (const juce::String& s) { hint (s); };

    for (int s = 0; s < kSlots; ++s)
    {
        cells.push_back (std::make_unique<SlotCell> (skin, proc, this, s));
        addAndMakeVisible (*cells.back());
    }

    auto& ap = proc.apvts;
    auto P = [&] (const juce::String& id) -> juce::RangedAudioParameter& { return *ap.getParameter (id); };
    linkT = make<ui::Toggle> (*this, "LINK: talk to the norns. Off: nothing goes out.", skin, P (ids::link), "LINK", "LINK OFF", this);
    syncCell = make<ui::MenuCell> (*this, "CLOCK: the host's tempo (and play / stop) to norns, when norns runs its internal clock. "
                                          "For phase-locked sync use Ableton Link on both.", skin, P (ids::sync), "CLOCK", this);
    psetBox = make<ui::NumberBox> (*this, "PSET: a number loads that norns preset (a SETBOOK scene can recall it). -- leaves norns alone.",
                                   skin, P (ids::pset), "PSET", this);

    addAndMakeVisible (hostField);
    hostField.setFont (ui::screenFont (19.0f));
    hostField.setIndents (6, 3);
    hostField.setText (proc.getHost(), false);
    hostField.setTooltip ("The norns: norns.local, or its IP (SYSTEM > WIFI on norns). host:port for another port.");
    hostField.onReturnKey = [this] { proc.setHost (hostField.getText()); hostField.setText (proc.getHost(), false); unfocusAllComponents(); };
    hostField.onFocusLost = [this] { proc.setHost (hostField.getText()); hostField.setText (proc.getHost(), false); };

    for (auto* b : { &loadBtn, &saveBtn, &themeBtn, &refreshBtn, &clearBtn }) addAndMakeVisible (b);
    loadBtn.text = "LOAD";
    loadBtn.setTooltip ("LOAD: load the PSET number on norns again.");
    loadBtn.onClick = [this] { const int n = juce::roundToInt (proc.apvts.getRawParameterValue (ids::pset)->load()); if (n > 0) proc.norns.psetLoad (n); };
    saveBtn.text = "SAVE";
    saveBtn.setTooltip ("SAVE: write norns' current state to the PSET number (01 when it is --).");
    saveBtn.onClick = [this]
    {
        auto* prm = proc.apvts.getParameter (ids::pset);
        int n = juce::roundToInt (prm->convertFrom0to1 (prm->getValue()));
        if (n <= 0) { n = 1; prm->setValueNotifyingHost (prm->convertTo0to1 (1.0f)); }
        proc.norns.psetSave (n);
    };
    refreshBtn.text = "RELIST";
    refreshBtn.setTooltip ("RELIST: ask norns for the parameter list again.");
    refreshBtn.onClick = [this] { proc.norns.requestDump(); };
    clearBtn.text = "UNBIND ALL";
    clearBtn.setTooltip ("UNBIND ALL: free every slot (their values stay).");
    clearBtn.onClick = [this] { for (int s = 0; s < kSlots; ++s) proc.unbindSlot (s); repaint(); };
    themeBtn.text = "THEME";
    themeBtn.onClick = [this] { themeMenu(); };

    proc.onMediaMessage = [this] (const juce::String& m) { hint (m); };

    // the AUDIO page
    tabs = std::make_unique<Tabs> (skin, [this] { return linkPage; }, [this] (int i) { setLinkPage (i); });
    addAndMakeVisible (*tabs);
    fromT = make<ui::Toggle> (*this, "FROM NORNS: norns' output (what goes to its headphones) comes out of this plug-in. "
                                     "Needs tether-audio on norns: the mod starts it. Latency = BUFFER, reported to the host.",
                              skin, P (ids::audioFrom), "FROM NORNS", "FROM NORNS", this);
    toT = make<ui::Toggle> (*this, "TO NORNS: this track's audio goes into norns' input (beside the ADC: softcut and the engine hear it).",
                            skin, P (ids::audioTo), "TO NORNS", "TO NORNS", this);
    thruT = make<ui::Toggle> (*this, "THRU: the track's own audio stays (delayed by the buffer, so it lines up with norns). Off: only norns.",
                              skin, P (ids::audioThru), "THRU", "THRU", this);
    bufferBox = make<ui::NumberBox> (*this, "BUFFER: how much audio waits before it plays, both ways. More rides out Wi-Fi hiccups; "
                                            "a cable runs well at 15-20 ms.", skin, P (ids::audioBuffer), "BUFFER", this);
    bitsCell = make<ui::MenuCell> (*this, "BITS: 24-bit (2.3 Mbit/s each way at 48 kHz) or 16-bit (1.5 Mbit/s).", skin, P (ids::audioBits), "BITS", this);
    fromGainBox = make<ui::NumberBox> (*this, "FROM LEVEL: norns' audio here.", skin, P (ids::audioFromGain), "FROM LEVEL", this);
    toGainBox = make<ui::NumberBox> (*this, "TO LEVEL: what goes to norns.", skin, P (ids::audioToGain), "TO LEVEL", this);
    for (auto* b : { bufferBox.get(), fromGainBox.get(), toGainBox.get() }) b->fontSize = 17.0f;
    if (! TetherProcessor::isFx) { toT->setVisible (false); thruT->setVisible (false); toGainBox->setVisible (false); }
    linkPage = juce::jlimit (0, 1, (int) proc.apvts.state.getProperty ("linkPage", 0));
    setLinkPage (linkPage);

    setSize (kW, kH);
    proc.editorOpened();
    startTimerHz (30);
}

TetherEditor::~TetherEditor()
{
    stopTimer();
    proc.onMediaMessage = nullptr;
    proc.editorClosed();
    setLookAndFeel (nullptr);
}

void TetherEditor::resized()
{
    header.setBounds (0, 0, kW, 104);
    nornsWin = { 12, 112, 572, 412 };
    paramsWin = { 596, 112, 572, 412 };
    slotsWin = { 12, 532, 860, 300 };
    linkWin = { 884, 532, 284, 300 };
    hintArea = { 14, 840, kW - 28, 18 };

    nornsPanel.setBounds (contentOf (nornsWin).reduced (2));
    paramList.setBounds (contentOf (paramsWin).reduced (4));

    auto sc = contentOf (slotsWin).reduced (4);
    const int cols = 8, rows = 4;
    const int cw = sc.getWidth() / cols, ch = sc.getHeight() / rows;
    for (int s = 0; s < kSlots; ++s)
        cells[(size_t) s]->setBounds (sc.getX() + (s % cols) * cw, sc.getY() + (s / cols) * ch, cw - 4, ch - 4);

    auto lc = contentOf (linkWin).reduced (8, 6);
    auto row = lc.removeFromTop (14);
    juce::ignoreUnused (row);
    hostField.setBounds (lc.removeFromTop (26));
    lc.removeFromTop (8);
    auto r2 = lc.removeFromTop (38);
    linkT->setBounds (r2.removeFromLeft (110).withTrimmedTop (12));
    r2.removeFromLeft (8);
    syncCell->setBounds (r2);
    lc.removeFromTop (6);
    auto r3 = lc.removeFromTop (40);
    psetBox->setBounds (r3.removeFromLeft (84));
    r3.removeFromLeft (8);
    loadBtn.setBounds (r3.removeFromLeft (80).withTrimmedTop (12));
    r3.removeFromLeft (6);
    saveBtn.setBounds (r3.withTrimmedTop (12));
    lc.removeFromTop (8);
    auto r4 = lc.removeFromBottom (28);
    themeBtn.setBounds (r4.removeFromRight (80));
    r4.removeFromRight (6);
    refreshBtn.setBounds (r4.removeFromRight (82));
    lc.removeFromBottom (6);
    clearBtn.setBounds (lc.removeFromBottom (28).removeFromRight (168));
    lc.removeFromBottom (6);
    infoArea = lc;

    // the title bar's right end: NET | AUDIO
    auto bar = linkWin.reduced (4).removeFromTop (17);
    tabs->setBounds (bar.removeFromRight (104).reduced (0, 1).translated (-4, 0));

    auto ac = contentOf (linkWin).reduced (8, 6);
    auto a1 = ac.removeFromTop (38).withTrimmedTop (12);
    fromT->setBounds (a1.removeFromLeft (a1.getWidth() / 2 - 3));
    a1.removeFromLeft (6);
    toT->setBounds (a1);
    ac.removeFromTop (6);
    auto a2 = ac.removeFromTop (40);
    bufferBox->setBounds (a2.removeFromLeft (84));
    a2.removeFromLeft (6);
    bitsCell->setBounds (a2.removeFromLeft (92));
    a2.removeFromLeft (6);
    thruT->setBounds (a2.withTrimmedTop (12));
    ac.removeFromTop (6);
    auto a3 = ac.removeFromTop (40);
    fromGainBox->setBounds (a3.removeFromLeft (a3.getWidth() / 2 - 3));
    a3.removeFromLeft (6);
    toGainBox->setBounds (a3);
    ac.removeFromTop (8);
    audioArea = ac;
}

void TetherEditor::setLinkPage (int i)
{
    linkPage = juce::jlimit (0, 1, i);
    proc.apvts.state.setProperty ("linkPage", linkPage, nullptr);
    const bool net = linkPage == 0;
    for (juce::Component* c : { (juce::Component*) &hostField, (juce::Component*) linkT.get(), (juce::Component*) syncCell.get(), (juce::Component*) psetBox.get(),
                                (juce::Component*) &loadBtn, (juce::Component*) &saveBtn, (juce::Component*) &clearBtn, (juce::Component*) &refreshBtn, (juce::Component*) &themeBtn })
        c->setVisible (net);
    fromT->setVisible (! net);
    bufferBox->setVisible (! net);
    bitsCell->setVisible (! net);
    fromGainBox->setVisible (! net);
    toT->setVisible (! net && TetherProcessor::isFx);
    thruT->setVisible (! net && TetherProcessor::isFx);
    toGainBox->setVisible (! net && TetherProcessor::isFx);
    if (tabs) tabs->repaint();
    repaint (linkWin);
}

void TetherEditor::paint (juce::Graphics& g)
{
    const auto& t = skin.t();
    g.fillAll (t.desk);
    // the desk's dither
    g.setColour (t.panelLo.withAlpha (0.25f));
    for (int y = 104; y < kH; y += 4)
        for (int x = (y / 4 & 1) * 2; x < kW; x += 4) g.fillRect (x, y, 1, 1);

    const auto s = proc.norns.snapshot (false);
    ui::draw::window (g, nornsWin, t, "NORNS", s.script != "none" && s.status == Status::Connected ? juce::String (s.script).toUpperCase() : juce::String());
    ui::draw::window (g, paramsWin, t, "PARAMETERS", s.expected > 0 ? juce::String (s.received) + "/" + juce::String (s.expected) : juce::String());
    int bound = 0;
    for (int i = 0; i < kSlots; ++i) bound += proc.slotId (i).isNotEmpty() ? 1 : 0;
    ui::draw::window (g, slotsWin, t, "SLOTS", juce::String (bound) + "/32 BOUND");
    paintLink (g, ui::draw::window (g, linkWin, t, "LINK"));

    g.setFont (ui::screenFont (17.0f));
    g.setColour (t.text);
    g.drawText (hintText, hintArea, juce::Justification::centredLeft, true);
}

void TetherEditor::paintLink (juce::Graphics& g, juce::Rectangle<int> c)
{
    const auto& t = skin.t();
    if (linkPage == 1) { ui::draw::smallLabel (g, "STREAM", c.reduced (8, 6).removeFromTop (12), t); paintAudio (g); return; }
    ui::draw::smallLabel (g, "NORNS (HOST OR IP)", c.reduced (8, 6).removeFromTop (14), t);

    // the state, in a screen well
    auto w = infoArea;
    if (w.getHeight() < 20) return;
    ui::draw::screenWell (g, w, t);
    const auto s = proc.norns.snapshot (false);
    g.setFont (ui::screenFont (18.0f));
    auto line = w.reduced (6, 2);
    auto put = [&] (const juce::String& a, const juce::String& b)
    {
        auto l = line.removeFromTop (18);
        g.setColour (t.ink.withAlpha (0.65f));
        g.drawText (a, l, juce::Justification::centredLeft);
        g.setColour (t.ink);
        g.drawText (b, l, juce::Justification::centredRight);
    };
    put ("STATE", statusName (s.status));
    put ("MOD", s.mod ? "v" + juce::String (s.version) : juce::String ("--"));
    put ("PING", s.latencyMs >= 0.0f ? juce::String (s.latencyMs, 1) + " ms" : juce::String ("--"));
}

// the AUDIO page's screen: the stream's state, both ends, and three meters
void TetherEditor::paintAudio (juce::Graphics& g)
{
    const auto& t = skin.t();
    auto w = audioArea;
    if (w.getHeight() < 30) return;
    ui::draw::screenWell (g, w, t);
    const auto st = proc.audio.stats();
    const bool from = proc.apvts.getRawParameterValue (ids::audioFrom)->load() > 0.5f;
    const bool to = TetherProcessor::isFx && proc.apvts.getRawParameterValue (ids::audioTo)->load() > 0.5f;

    // meters on the right: FROM (norns), TO L / R
    auto meters = w.removeFromRight (46).reduced (5, 6);
    auto bar = [&] (juce::Rectangle<int> r, float v, const juce::String& l)
    {
        auto lab = r.removeFromBottom (11);
        g.setColour (t.ink.withAlpha (0.25f));
        g.drawRect (r);
        const float db = juce::jlimit (0.0f, 1.0f, (juce::Decibels::gainToDecibels (v, -60.0f) + 60.0f) / 60.0f);
        const int h = juce::roundToInt ((float) (r.getHeight() - 2) * db);
        g.setColour (v > 0.98f ? juce::Colour (0xffe8302a) : ui::readable (t, t.thermal (0.3f + 0.6f * db)));
        g.fillRect (r.getX() + 1, r.getBottom() - 1 - h, r.getWidth() - 2, h);
        g.setColour (t.ink.withAlpha (0.7f));
        g.setFont (ui::screenFont (12.0f));
        g.drawText (l, lab, juce::Justification::centred);
    };
    if (TetherProcessor::isFx)
    {
        const int mw = meters.getWidth() / 3;
        bar (meters.removeFromLeft (mw).reduced (1, 0), st.inLevel, "IN");
        bar (meters.removeFromLeft (mw).reduced (1, 0), st.outL, "L");
        bar (meters.reduced (1, 0), st.outR, "R");
    }
    else bar (meters.withSizeKeepingCentre (14, meters.getHeight()), st.inLevel, "IN");

    g.setFont (ui::screenFont (17.0f));
    auto line = w.reduced (6, 3);
    auto put = [&] (const juce::String& a, const juce::String& b)
    {
        if (line.getHeight() < 16) return;
        auto l = line.removeFromTop (17);
        g.setColour (t.ink.withAlpha (0.65f));
        g.drawText (a, l, juce::Justification::centredLeft);
        g.setColour (t.ink);
        g.drawText (b, l, juce::Justification::centredRight);
    };
    juce::String state = "OFF";
    if (from || to) state = st.daemon ? (st.playing || ! from ? "STREAMING" : "BUFFERING") : "STARTING";
    put ("AUDIO", state);
    if (st.daemon)
    {
        put ("NORNS", juce::String (st.norns.rate) + " / " + juce::String (st.norns.period) + (st.norns.xruns > 0 ? "  XR " + juce::String (st.norns.xruns) : juce::String()));
        if (from) put ("HERE", juce::String (st.fillMs, 1) + " ms  " + juce::String (juce::roundToInt (st.driftPpm)) + " ppm");
        if (to) put ("THERE", juce::String (st.norns.fillMs, 1) + " ms");
        put ("LOST", juce::String (st.missing + st.norns.lost) + "  GAPS " + juce::String (st.underruns + st.norns.underruns));
    }
    else if (from || to) put ("", "waiting for tether-audio");
    else if (! TetherProcessor::isFx) put ("", "TETHER FX sends audio");
}

void TetherEditor::pump (float dt)
{
    header.advance (dt);
    nornsPanel.pump (dt);
    paramList.pump();

    const auto s = proc.norns.snapshot (false);
    header.status[0] = juce::String (statusName (s.status));
    header.status[1] = s.script == "none" ? juce::String ("no script") : juce::String (s.script).toUpperCase();
    header.status[2] = s.latencyMs >= 0.0f ? juce::String (s.expected) + " PARAMS  " + juce::String (juce::roundToInt (s.latencyMs)) + " MS" : juce::String (proc.getHost());

    // the hardware sets off the header's plasma
    for (int k = 0; k < 3; ++k)
    {
        const float e = proc.live.e[(size_t) k].load();
        if (lastE[k] >= 0.0f && std::abs (e - lastE[k]) > 1.0e-5f) header.burst (0.2f + 0.3f * (float) k, 0.6f);
        lastE[k] = e;
    }
    header.repaint();

    for (auto& c : cells)
    {
        const int n = (int) (&c - &cells[0]);
        if (std::abs (proc.live.slot[(size_t) n].load() - c->shownValue) > 1.0e-4f || proc.slotModulated (n) != c->shownMod) c->repaint();
    }
    const auto ps = proc.norns.paramsSerial();
    if (ps != paramsSeen) { paramsSeen = ps; for (auto& c : cells) c->repaint(); }
    repaint (paramsWin.withHeight (24));
    repaint (nornsWin.withHeight (24));
    repaint (slotsWin.withHeight (24));
    repaint (infoArea);
    if (linkPage == 1) repaint (audioArea);
}

void TetherEditor::setTheme (int i)
{
    skin.index = juce::jlimit (0, ui::numThemes() - 1, i);
    proc.apvts.state.setProperty ("theme", skin.index, nullptr);
    look.refreshColours();
    hostField.applyColourToAllText (skin.t().ink);
    repaint();
    for (auto* c : getChildren()) c->repaint();
}

void TetherEditor::themeMenu()
{
    juce::PopupMenu m;
    for (int i = 0; i < ui::numThemes(); ++i) m.addItem (i + 1, juce::String (ui::theme (i).name).toUpperCase(), true, i == skin.index);
    juce::Component::SafePointer<TetherEditor> safe (this);
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&themeBtn), [safe] (int r) { if (safe != nullptr && r > 0) safe->setTheme (r - 1); });
}
