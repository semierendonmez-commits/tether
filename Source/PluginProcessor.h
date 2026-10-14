// TETHER: norns from the DAW. The tether mod on norns answers over OSC (docs/PROTOCOL.md); this
// plug-in mirrors the screen, plays the keys and encoders, lists the running script's parameters,
// and binds 32 host parameters (SLOT 1-32) to any of them, so the host automates norns and
// CONDUIT patches into it. The norns keys and encoders come back as family events and values.
// Audio (docs/AUDIO.md): norns' output streams in through tether-audio on norns. TETHER (an
// instrument) plays it; TETHER FX also sends its input to norns' input and keeps its dry signal.
#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <family/FamilyLink.h>
#include "FamilyManifest.h"
#include "Params.h"
#include "engine/NornsLink.h"
#include "engine/AudioLink.h"
#include <memory>

class TetherProcessor : public juce::AudioProcessor, private juce::Timer
{
public:
    TetherProcessor();
    ~TetherProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout&) const override;
    using juce::AudioProcessor::processBlock;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;
    void updateTrackProperties (const TrackProperties&) override;

    juce::AudioProcessorValueTreeState apvts;
    family::Link link { *this, apvts, family::kManifest };
    tether::NornsLink norns;
    tether::AudioLink audio;
    static constexpr bool isFx = ! JucePlugin_IsSynth;   // TETHER FX: sends its input to norns

    // ---- the slots (message thread) ---------------------------------------------------------------
    // binds slot s to a norns parameter; the slot jumps to the parameter's value, so nothing moves
    void bindSlot (int s, const juce::String& paramId, const juce::String& paramName);
    void unbindSlot (int s) { bindSlot (s, {}, {}); }
    juce::String slotId (int s) const;
    juce::String slotName (int s) const;                 // the norns parameter's name, or ""
    int slotOf (const juce::String& paramId) const;       // -1 if not bound
    juce::RangedAudioParameter& slotParam (int s);
    bool slotModulated (int s) const { return slotRaw[(size_t) s] != nullptr && slotRaw[(size_t) s]->modulated(); }

    void setHost (const juce::String&);
    juce::String getHost() const { return apvts.state.getProperty ("host", "norns.local").toString(); }

    // the editors open: the screen streams only while one is (message thread)
    void editorOpened();
    void editorClosed();

    // ---- the screen: colour style, snapshot, recording (message thread) -------------------------------
    int getScreenStyle() const;
    void setScreenStyle (int);
    int getThemeIndex() const { return (int) apvts.state.getProperty ("theme", 0); }
    juce::File mediaFolder() const;                       // ~/Music/TETHER
    juce::File snapshot();                                // a PNG of the last frame (x4); {} if no frame yet
    void startRecording();
    juce::File stopRecording();                           // the GIF it is writing (ready when ! encoding())
    bool isRecording() const noexcept { return recording; }
    bool encoding() const;
    double recordingSeconds() const;
    juce::File lastRecording() const { return lastRec; }
    std::function<void (const juce::String&)> onMediaMessage;   // "saved ...", for the editor's hint line
    static constexpr double kMaxRecordSeconds = 600.0;
    static constexpr int kGifScale = 4;

    // ---- live state for the editor ------------------------------------------------------------------
    struct Live
    {
        std::array<std::atomic<float>, tether::kSlots> slot {};    // what goes out (knob + CONDUIT)
        std::array<std::atomic<float>, 3> e {};                    // E1-E3 as values (family outputs)
        std::atomic<double> bpm { 120.0 };
        std::atomic<bool> playing { false };
    } live;

    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    static BusesProperties buses();
    void tick() { timerCallback(); }         // tests and tools: one message-thread pass

private:
    class SlotParameter;
    void timerCallback() override;
    void refreshSlotNames();

    std::array<SlotParameter*, tether::kSlots> slots {};
    std::array<const family::Param*, tether::kSlots> slotRaw {};
    const family::Param* syncRaw = nullptr;
    std::atomic<float>* linkRaw = nullptr, *psetRaw = nullptr;
    std::atomic<float>* aFrom = nullptr, *aTo = nullptr, *aBuffer = nullptr, *aBits = nullptr, *aFromGain = nullptr, *aToGain = nullptr, *aThru = nullptr;
    void updateAudio();
    void audioBlock (juce::AudioBuffer<float>&);
    int latencyNow = -1;
    std::string audioTarget;
    double sampleRateNow = 48000.0;
    // TETHER FX: the dry signal waits as long as the reported latency, so it stays aligned
    juce::AudioBuffer<float> dryDelay;
    int dryWrite = 0;
    std::atomic<int> dryLatency { 0 };

    int pK[3] { -1, -1, -1 }, pE[3] { -1, -1, -1 }, pPress[3] { -1, -1, -1 }, pScript = -1;
    float eAcc[3] { 0.5f, 0.5f, 0.5f };      // audio thread

    // message thread
    int editors = 0;
    void updateScreenFps();
    juce::String mediaName (const juce::String& ext) const;

    // recording (message thread): distinct frames, each with the time it appeared
    struct EncodeJob;
    bool recording = false;
    double recStart = 0.0;
    std::vector<uint8_t> recFrames;                       // 8192 bytes a frame
    std::vector<double> recTimes;
    std::array<uint8_t, tether::kScreenW * tether::kScreenH> lastFrame {};
    uint32_t lastFrameSerial = 0;
    bool haveFrame = false;
    std::shared_ptr<EncodeJob> job;
    juce::File lastRec;
    int psetLoaded = 0;
    uint32_t listSeen = 0;
    std::string scriptSeen;
    juce::String trackName;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TetherProcessor)
};
