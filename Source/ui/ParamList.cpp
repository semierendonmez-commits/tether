#include "ParamList.h"
#include "../PluginProcessor.h"
#include "Ink.h"

namespace ui
{

ParamList::ParamList (Skin& s, TetherProcessor& p) : skin (s), proc (p)
{
    addAndMakeVisible (filter);
    filter.setTextToShowWhenEmpty ("filter: name or id", skin.t().textDim);
    filter.setFont (screenFont (18.0f));
    filter.setIndents (6, 2);
    filter.onTextChange = [this] { refilter(); };
    filter.onEscapeKey = [this] { filter.clear(); refilter(); };
    addAndMakeVisible (bar);
    bar.addListener (this);
    bar.setAutoHide (false);
    setRepaintsOnMouseActivity (false);
}

ParamList::~ParamList() { bar.removeListener (this); }

void ParamList::resized()
{
    auto r = getLocalBounds();
    filter.setBounds (r.removeFromTop (24).reduced (0, 1));
    r.removeFromTop (4);
    bar.setBounds (r.removeFromRight (12));
    listArea = r;
    refilter();
}

void ParamList::pump()
{
    const auto s = proc.norns.paramsSerial();
    if (s == serial) return;
    serial = s;
    all = proc.norns.snapshot (true).params;
    refilter();
}

void ParamList::refilter()
{
    const auto q = filter.getText().trim().toLowerCase();
    shown.clear();
    for (int i = 0; i < (int) all.size(); ++i)
    {
        const auto& p = all[(size_t) i];
        if (! p.known) continue;
        if (q.isNotEmpty() && ! juce::String (p.name).toLowerCase().contains (q) && ! juce::String (p.id).toLowerCase().contains (q)) continue;
        shown.push_back (i);
    }
    const int rows = juce::jmax (1, listArea.getHeight() / kRowH);
    bar.setRangeLimits (0.0, (double) juce::jmax ((int) shown.size(), rows));
    bar.setCurrentRange (bar.getCurrentRangeStart(), (double) rows);
    repaint();
}

juce::Rectangle<int> ParamList::rowRect (int vi) const
{
    const int first = (int) bar.getCurrentRangeStart();
    return { listArea.getX(), listArea.getY() + (vi - first) * kRowH, listArea.getWidth(), kRowH };
}

juce::Rectangle<int> ParamList::barOf (juce::Rectangle<int> row) const
{
    return row.withTrimmedLeft (34 + 150).withTrimmedRight (20).reduced (2, 3);
}

int ParamList::rowAt (juce::Point<int> pt) const
{
    if (! listArea.contains (pt)) return -1;
    const int vi = (int) bar.getCurrentRangeStart() + (pt.y - listArea.getY()) / kRowH;
    return vi >= 0 && vi < (int) shown.size() ? vi : -1;
}

void ParamList::paint (juce::Graphics& g)
{
    const auto& t = skin.t();
    draw::screenWell (g, listArea, t);
    if (shown.empty())
    {
        g.setColour (t.ink.withAlpha (0.7f));
        g.setFont (screenFont (20.0f));
        const auto msg = all.empty() ? juce::String (proc.norns.status() == tether::Status::Connected ? "the list is on its way" : "no norns yet")
                                     : juce::String ("nothing matches the filter");
        g.drawText (msg, listArea, juce::Justification::centred);
        return;
    }

    juce::Graphics::ScopedSaveState ss (g);
    g.reduceClipRegion (listArea);
    const int first = (int) bar.getCurrentRangeStart();
    const int rows = listArea.getHeight() / kRowH + 1;
    for (int vi = first; vi < juce::jmin ((int) shown.size(), first + rows); ++vi)
    {
        const auto& p = all[(size_t) shown[(size_t) vi]];
        auto r = rowRect (vi);
        if (vi == hover || vi == dragRow)
        {
            g.setColour (t.ink.withAlpha (0.08f));
            g.fillRect (r);
        }
        if (((vi - first) & 1) == 1)
        {
            g.setColour (t.ink.withAlpha (0.03f));
            g.fillRect (r);
        }

        // the slot badge
        const int slot = proc.slotOf (p.id);
        auto badge = r.removeFromLeft (34).reduced (3, 4);
        if (slot >= 0)
        {
            const bool mod = proc.slotModulated (slot);
            g.setColour (mod ? t.accent2 : t.accent);
            g.fillRect (badge);
            g.setColour (t.screen);
            g.setFont (screenFont (16.0f));
            g.drawText ("S" + juce::String (slot + 1), badge, juce::Justification::centred);
        }

        const auto dimText = p.hidden ? dim (t) : t.ink;
        g.setColour (dimText);
        g.setFont (screenFont (18.0f));
        g.drawText (juce::String (p.name), r.removeFromLeft (150).withTrimmedRight (6), juce::Justification::centredLeft, true);

        auto arrow = r.removeFromRight (20);
        juce::Path tri;
        const auto c = arrow.getCentre().toFloat();
        tri.addTriangle (c.x - 3.0f, c.y - 4.5f, c.x - 3.0f, c.y + 4.5f, c.x + 3.5f, c.y);
        g.setColour (vi == hover ? t.accent2 : t.inkDim.brighter (0.4f));
        g.fillPath (tri);

        // the value
        auto b = barOf (rowRect (vi));
        g.setColour (t.panelLo.withAlpha (0.6f));
        g.drawRect (b);
        auto in = b.reduced (1);
        if (p.type == tether::TTrigger)
        {
            g.setColour (t.inkDim);
            g.fillRect (in);
            g.setColour (t.ink);
            g.setFont (screenFont (16.0f));
            g.drawText ("TRIGGER", in, juce::Justification::centred);
            continue;
        }
        const float v = slot >= 0 ? proc.live.slot[(size_t) slot].load() : p.raw;
        const int w = juce::roundToInt ((float) in.getWidth() * v);
        g.setColour (readable (t, t.thermal (0.3f + 0.6f * v)).withAlpha (p.hidden ? 0.4f : 0.85f));
        g.fillRect (in.withWidth (w));
        if (p.steps > 1 && p.steps <= 24)
        {
            g.setColour (t.screen.withAlpha (0.6f));
            for (int k = 1; k < p.steps; ++k)
            {
                const int x = in.getX() + juce::roundToInt ((float) in.getWidth() * (float) k / (float) (p.steps - 1)) - 1;
                if (k < p.steps - 1) g.fillRect (x, in.getY(), 1, in.getHeight());
            }
        }
        g.setFont (screenFont (17.0f));
        const auto txt = juce::String (p.text);
        g.setColour (t.screen.withAlpha (0.75f));
        g.drawText (txt, in.translated (1, 1).reduced (6, 0), juce::Justification::centredRight, true);
        g.setColour (t.ink);
        g.drawText (txt, in.reduced (6, 0), juce::Justification::centredRight, true);
    }
}

void ParamList::setFromX (int vi, int x, bool start)
{
    const auto& p = all[(size_t) shown[(size_t) vi]];
    const auto b = barOf (rowRect (vi)).reduced (1);
    float v = juce::jlimit (0.0f, 1.0f, (float) (x - b.getX()) / (float) juce::jmax (1, b.getWidth()));
    if (p.steps > 1) v = std::round (v * (float) (p.steps - 1)) / (float) (p.steps - 1);

    const int slot = proc.slotOf (p.id);
    if (slot >= 0)
    {
        // a bound parameter moves through its slot: the host sees (and records) the move
        auto& prm = proc.slotParam (slot);
        if (start) { prm.beginChangeGesture(); dragSlot = slot; }
        prm.setValueNotifyingHost (v);
    }
    else
    {
        proc.norns.setParam (p.id, v);
        all[(size_t) shown[(size_t) vi]].raw = v;
    }
    repaint (rowRect (vi));
}

void ParamList::mouseDown (const juce::MouseEvent& e)
{
    const int vi = rowAt (e.getPosition());
    if (vi < 0) return;
    const auto& p = all[(size_t) shown[(size_t) vi]];
    const auto r = rowRect (vi);
    if (e.mods.isPopupMenu() || e.x >= r.getRight() - 20 || e.x < r.getX() + 34) { showBindMenu (vi); return; }
    if (! barOf (r).contains (e.getPosition())) return;
    if (p.type == tether::TTrigger) { proc.norns.setParam (p.id, 1.0f); return; }
    if (p.type == tether::TBinary) { setFromX (vi, p.raw > 0.5f ? barOf (r).getX() : barOf (r).getRight(), true); dragRow = -1; mouseUp (e); return; }
    dragRow = vi;
    setFromX (vi, e.x, true);
}

void ParamList::mouseDrag (const juce::MouseEvent& e)
{
    if (dragRow >= 0 && dragRow < (int) shown.size()) setFromX (dragRow, e.x, false);
}

void ParamList::mouseUp (const juce::MouseEvent&)
{
    if (dragSlot >= 0) proc.slotParam (dragSlot).endChangeGesture();
    dragSlot = -1;
    dragRow = -1;
}

void ParamList::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w)
{
    if (e.mods.isCommandDown() || e.mods.isAltDown())
    {
        // fine adjust the parameter under the mouse
        const int vi = rowAt (e.getPosition());
        if (vi < 0) return;
        auto& p = all[(size_t) shown[(size_t) vi]];
        if (p.type == tether::TTrigger) return;
        const float step = p.steps > 1 ? 1.0f / (float) (p.steps - 1) : 0.01f;
        const float v = juce::jlimit (0.0f, 1.0f, p.raw + (w.deltaY > 0 ? step : -step));
        const auto b = barOf (rowRect (vi)).reduced (1);
        setFromX (vi, b.getX() + juce::roundToInt (v * (float) b.getWidth()), true);
        mouseUp (e);
        return;
    }
    bar.setCurrentRangeStart (bar.getCurrentRangeStart() - (double) w.deltaY * 12.0);
}

void ParamList::mouseMove (const juce::MouseEvent& e)
{
    const int h = rowAt (e.getPosition());
    if (h != hover)
    {
        hover = h;
        repaint();
        if (onHint && h >= 0)
        {
            const auto& p = all[(size_t) shown[(size_t) h]];
            const int slot = proc.slotOf (p.id);
            onHint (juce::String (p.name) + " (" + p.id + "): drag to set"
                    + (slot >= 0 ? ", moves SLOT " + juce::String (slot + 1) : juce::String (", right-click to bind to a slot")));
        }
    }
}

void ParamList::showBindMenu (int vi)
{
    if (vi < 0 || vi >= (int) shown.size()) return;
    const auto p = all[(size_t) shown[(size_t) vi]];
    const juce::String id (p.id), name (p.name);
    const int cur = proc.slotOf (id);
    juce::PopupMenu m;
    m.addSectionHeader (name.toUpperCase() + "  (" + id + ")");
    if (cur >= 0) m.addItem (1000, "unbind from SLOT " + juce::String (cur + 1));
    int firstFree = -1;
    for (int s = 0; s < tether::kSlots; ++s) if (proc.slotId (s).isEmpty()) { firstFree = s; break; }
    if (cur < 0 && firstFree >= 0) m.addItem (2000 + firstFree, "bind to the first free slot (SLOT " + juce::String (firstFree + 1) + ")");
    juce::PopupMenu slotsMenu;
    for (int s = 0; s < tether::kSlots; ++s)
    {
        const auto bound = proc.slotName (s);
        slotsMenu.addItem (2000 + s, "SLOT " + juce::String (s + 1) + (bound.isNotEmpty() ? "   " + bound : juce::String()), true, s == cur);
        if (s == 15) slotsMenu.addSeparator();   // 1-16 are on the family bus
    }
    m.addSubMenu ("bind to", slotsMenu);
    juce::Component::SafePointer<ParamList> safe (this);
    m.showMenuAsync (juce::PopupMenu::Options().withTargetScreenArea (localAreaToGlobal (rowRect (vi))),
                     [safe, id, name, cur] (int r)
                     {
                         if (safe == nullptr || r == 0) return;
                         if (r == 1000 && cur >= 0) safe->proc.unbindSlot (cur);
                         else if (r >= 2000)
                         {
                             if (cur >= 0) safe->proc.unbindSlot (cur);
                             safe->proc.bindSlot (r - 2000, id, name);
                         }
                         safe->repaint();
                     });
}

} // namespace ui
