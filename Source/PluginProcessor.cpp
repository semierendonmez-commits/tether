#include "PluginProcessor.h"
#include "PluginEditor.h"

using namespace tether;

//==============================================================================
// A slot's host name follows its binding ("S3 cutoff"), so the automation lane says what it moves.
class TetherProcessor::SlotParameter : public juce::AudioParameterFloat
{
public:
    SlotParameter (int s)
        : juce::AudioParameterFloat (juce::ParameterID { ids::slot (s), 1 }, "Slot " + juce::String (s + 1),
                                     juce::NormalisableRange<float> (0.0f, 1.0f), 0.0f,
                                     juce::AudioParameterFloatAttributes().withStringFromValueFunction (
                                         [] (float v, int) { return juce::String (juce::roundToInt (v * 1000.0f) / 10.0f, 1) + "%"; })),
          index (s)
    {}

    juce::String getName (int maximumLength) const override
    {
        const juce::SpinLock::ScopedLockType l (lock);
        const auto n = label.isEmpty() ? "Slot " + juce::String (index + 1) : "S" + juce::String (index + 1) + " " + label;
        return n.substring (0, maximumLength);
    }
    void setLabel (const juce::String& s)
    {
        const juce::SpinLock::ScopedLockType l (lock);
        label = s;
    }

private:
    const int index;
    mutable juce::SpinLock lock;
    juce::String label;
};

//==============================================================================
juce::AudioProcessorValueTreeState::ParameterLayout TetherProcessor::createLayout()
{
    using namespace juce;
    AudioProcessorValueTreeState::ParameterLayout l;
    for (int s = 0; s < kSlots; ++s) l.add (std::make_unique<SlotParameter> (s));
    l.add (std::make_unique<AudioParameterBool> (ParameterID { ids::link, 1 }, "Link", true));
    l.add (std::make_unique<AudioParameterChoice> (ParameterID { ids::sync, 1 }, "Clock sync", StringArray { "OFF", "TEMPO", "TEMPO + PLAY" }, 0));
    l.add (std::make_unique<AudioParameterInt> (ParameterID { ids::pset, 1 }, "PSET", 0, 99, 0,
                                                AudioParameterIntAttributes().withStringFromValueFunction (
                                                    [] (int v, int) { return v == 0 ? String ("--") : String (v).paddedLeft ('0', 2); })));
    return l;
}

TetherProcessor::TetherProcessor()
    : AudioProcessor (BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "TETHER", createLayout())
{
    for (int s = 0; s < kSlots; ++s)
    {
        slots[(size_t) s] = dynamic_cast<SlotParameter*> (apvts.getParameter (ids::slot (s)));
        slotRaw[(size_t) s] = link.raw (ids::slot (s));
    }
    syncRaw = link.raw (ids::sync);
    linkRaw = apvts.getRawParameterValue (ids::link);
    psetRaw = apvts.getRawParameterValue (ids::pset);

    for (int k = 0; k < 3; ++k)
    {
        const juce::String n (k + 1);
        pK[k] = link.port (("K" + n).toRawUTF8());
        pE[k] = link.port (("E" + n).toRawUTF8());
        pPress[k] = link.port (("PRESS" + n).toRawUTF8());
        link.setValue (pE[k], eAcc[k]);
        live.e[(size_t) k].store (eAcc[k]);
    }
    pScript = link.port ("SCRIPT");

    norns.setHost (getHost().toStdString());
    norns.start();
    startTimerHz (30);
}

TetherProcessor::~TetherProcessor()
{
    stopTimer();
    norns.stop();
}

bool TetherProcessor::isBusesLayoutSupported (const BusesLayout& l) const
{
    const auto o = l.getMainOutputChannelSet();
    return o == juce::AudioChannelSet::stereo() || o == juce::AudioChannelSet::mono() || o == juce::AudioChannelSet::disabled();
}

void TetherProcessor::prepareToPlay (double, int) {}

void TetherProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals nd;
    buffer.clear();
    midi.clear();

    double bpm = live.bpm.load (std::memory_order_relaxed), ppq = -1.0;
    bool playing = false;
    if (auto* ph = getPlayHead())
        if (auto pos = ph->getPosition())
        {
            if (auto b = pos->getBpm()) bpm = *b;
            if (auto q = pos->getPpqPosition()) ppq = *q;
            playing = pos->getIsPlaying();
        }
    live.bpm.store (bpm, std::memory_order_relaxed);
    live.playing.store (playing, std::memory_order_relaxed);
    norns.setClock (bpm, playing, juce::roundToInt (syncRaw->load()));

    // the slots: the knob (host automation) plus whatever CONDUIT patches into it
    for (int s = 0; s < kSlots; ++s)
    {
        const float v = juce::jlimit (0.0f, 1.0f, slotRaw[(size_t) s]->load());
        norns.setSlot (s, v);
        live.slot[(size_t) s].store (v, std::memory_order_relaxed);
    }

    // family triggers press norns keys
    for (int k = 0; k < 3; ++k)
        if (link.triggered (pPress[k])) norns.pressKey (k + 1);

    // the norns hardware, out to the family
    HwEvent e;
    while (norns.popHw (e))
    {
        const int k = e.n - 1;
        if (e.kind == 0)
        {
            if (e.v > 0) link.emit (pK[k], e.n, 127, 0.0f, 0, ppq);
        }
        else
        {
            eAcc[k] = juce::jlimit (0.0f, 1.0f, eAcc[k] + (float) e.v / 96.0f);
            link.setValue (pE[k], eAcc[k]);
            live.e[(size_t) k].store (eAcc[k], std::memory_order_relaxed);
        }
    }
}

//==============================================================================
void TetherProcessor::timerCallback()
{
    norns.setEnabled (linkRaw->load() > 0.5f);

    // PSET: a new number loads that preset on norns (SETBOOK recalls it with a scene)
    const int ps = juce::roundToInt (psetRaw->load());
    if (ps != psetLoaded)
    {
        psetLoaded = ps;
        if (ps > 0) norns.psetLoad (ps);
    }

    // another script: tell the family, rename the slots that point into it
    const uint32_t ls = norns.listSerial();
    if (ls != listSeen)
    {
        listSeen = ls;
        const auto snap = norns.snapshot (false);
        if (snap.script != scriptSeen && snap.script != "none")
        {
            scriptSeen = snap.script;
            link.messageType (familybus::EvDataLoaded, 0, 0, 0.0f, family::hash32 (snap.script.c_str()));
        }
        refreshSlotNames();
    }
}

void TetherProcessor::refreshSlotNames()
{
    bool changed = false;
    for (int s = 0; s < kSlots; ++s)
    {
        const auto id = slotId (s);
        juce::String label;
        if (id.isNotEmpty())
        {
            ParamInfo p;
            label = norns.findParam (id.toStdString(), p) ? juce::String (p.name) : slotName (s);
            if (label.isEmpty()) label = id;
        }
        const auto before = slots[(size_t) s]->getName (64);
        slots[(size_t) s]->setLabel (label);
        changed |= before != slots[(size_t) s]->getName (64);
    }
    if (changed) updateHostDisplay (ChangeDetails().withParameterInfoChanged (true));
}

void TetherProcessor::bindSlot (int s, const juce::String& paramId, const juce::String& paramName)
{
    if (s < 0 || s >= kSlots) return;
    auto tree = apvts.state.getOrCreateChildWithName ("SLOTS", nullptr);
    auto node = tree.getOrCreateChildWithName ("S" + juce::String (s + 1), nullptr);
    if (paramId.isEmpty()) tree.removeChild (node, nullptr);
    else { node.setProperty ("id", paramId, nullptr); node.setProperty ("name", paramName, nullptr); }

    // the slot starts where the norns parameter is, then the binding takes over: no jump
    ParamInfo p;
    if (paramId.isNotEmpty() && norns.findParam (paramId.toStdString(), p))
    {
        auto& prm = *slots[(size_t) s];
        prm.beginChangeGesture();
        prm.setValueNotifyingHost (p.raw);
        prm.endChangeGesture();
        norns.setSlot (s, p.raw);
    }
    norns.bindSlot (s, paramId.toStdString());
    refreshSlotNames();
}

juce::String TetherProcessor::slotId (int s) const
{
    return apvts.state.getChildWithName ("SLOTS").getChildWithName ("S" + juce::String (s + 1)).getProperty ("id").toString();
}

juce::String TetherProcessor::slotName (int s) const
{
    return apvts.state.getChildWithName ("SLOTS").getChildWithName ("S" + juce::String (s + 1)).getProperty ("name").toString();
}

int TetherProcessor::slotOf (const juce::String& paramId) const
{
    for (int s = 0; s < kSlots; ++s)
        if (slotId (s) == paramId) return s;
    return -1;
}

void TetherProcessor::setHost (const juce::String& h)
{
    const auto t = h.trim();
    apvts.state.setProperty ("host", t.isEmpty() ? juce::String ("norns.local") : t, nullptr);
    norns.setHost (getHost().toStdString());
}

void TetherProcessor::updateTrackProperties (const TrackProperties& p)
{
    if (p.name.has_value())
    {
        link.setInstanceName (*p.name);
        trackName = *p.name;
        norns.setClientName (trackName.toStdString());
    }
}

void TetherProcessor::editorOpened()
{
    ++editors;
    norns.setScreenFps (30);
}

void TetherProcessor::editorClosed()
{
    editors = juce::jmax (0, editors - 1);
    if (editors == 0) norns.setScreenFps (0);
}

//==============================================================================
void TetherProcessor::getStateInformation (juce::MemoryBlock& dest)
{
    if (auto xml = apvts.copyState().createXml()) copyXmlToBinary (*xml, dest);
}

void TetherProcessor::setStateInformation (const void* data, int size)
{
    if (auto xml = getXmlFromBinary (data, size))
    {
        auto state = juce::ValueTree::fromXml (*xml);
        if (! state.hasType (apvts.state.getType())) return;
        apvts.replaceState (state);
        psetLoaded = juce::roundToInt (psetRaw->load());   // opening a session does not reload the preset on norns
        norns.setHost (getHost().toStdString());
        for (int s = 0; s < kSlots; ++s) norns.bindSlot (s, slotId (s).toStdString());
        refreshSlotNames();
    }
}

juce::AudioProcessorEditor* TetherProcessor::createEditor() { return new TetherEditor (*this); }

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new TetherProcessor(); }

juce::RangedAudioParameter& TetherProcessor::slotParam (int s) { return *slots[(size_t) s]; }
