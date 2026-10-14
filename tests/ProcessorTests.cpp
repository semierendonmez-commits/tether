// TETHER processor tests (JUCE): the plug-in against the stand-in norns (the real mod over UDP),
// the slots, host names, PSET, clock, state, the family bus (CONDUIT into a slot, PRESS triggers,
// norns hardware out as events and values), and the editor.
#include <juce_audio_utils/juce_audio_utils.h>
#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "NornsSim.h"

static int checks = 0, failures = 0;
#define CHECK(cond, ...) do { ++checks; if (! (cond)) { ++failures; std::printf ("FAIL %s:%d  %s  ", __FILE__, __LINE__, #cond); std::printf (__VA_ARGS__); std::printf ("\n"); } } while (0)

using namespace tether;
namespace fb = familybus;

struct Head : juce::AudioPlayHead
{
    double bpm = 140.0, ppq = 0.0;
    bool playing = true;
    juce::Optional<PositionInfo> getPosition() const override
    {
        PositionInfo p;
        p.setBpm (bpm);
        p.setPpqPosition (ppq);
        p.setIsPlaying (playing);
        return p;
    }
};

struct Rig
{
    TetherProcessor proc;
    Head head;
    juce::AudioBuffer<float> buf { 2, 480 };
    juce::MidiBuffer midi;
    Rig()
    {
        proc.setPlayConfigDetails (0, 2, 48000.0, 480);
        proc.prepareToPlay (48000.0, 480);
        proc.setPlayHead (&head);
    }
    void block()
    {
        midi.clear();
        proc.processBlock (buf, midi);
        head.ppq += 480.0 * head.bpm / 60.0 / 48000.0;
    }
    // the message thread and the audio thread, for a while, until f() (real time: the link is a thread)
    template <typename F> bool until (F&& f, int ms = 3000)
    {
        for (int t = 0; t < ms; t += 10)
        {
            if (f()) return true;
            proc.tick();
            block();
            juce::Thread::sleep (10);
        }
        return f();
    }
    void run (int ms) { until ([] { return false; }, ms); }
};

static float simGet (Bridge& b, const std::string& id)
{
    const auto v = b.cmd ("GET\t" + id);
    return v.empty() ? -1.0f : (float) std::atof (v[0].c_str());
}

int main()
{
    ::unlink ("/dev/shm/semi.family.v1");
    ::unlink ("/dev/shm/datascreen.bus.1");
    juce::ScopedJuceInitialiser_GUI init;

    Bridge norns;
    if (! norns.start()) { std::printf ("no lua / stand-in norns: processor test skipped\n"); return 0; }

    {
        Rig r;
        auto& p = r.proc;

        std::printf ("family: manifest, bus, ports\n");
        for (const auto& e : p.link.getManifest().errors) std::printf ("  manifest: %s\n", e.toRawUTF8());
        CHECK (p.link.getManifest().errors.isEmpty(), "manifest errors");
        CHECK (p.link.onBus(), "on the family bus");
        for (auto* n : { "K1", "K2", "K3", "E1", "E2", "E3", "SCRIPT", "SLOT1", "SLOT16", "PRESS2" })
            CHECK (p.link.port (n) >= 0, "port %s", n);
        CHECK (p.link.port ("SLOT17") < 0, "slots 17-32 are host-only");

        std::printf ("defaults: norns.local, no binding, slots named Slot n\n");
        CHECK (p.getHost() == "norns.local", "default host");
        CHECK (p.slotParam (0).getName (32) == "Slot 1", "%s", p.slotParam (0).getName (32).toRawUTF8());

        std::printf ("connect to the stand-in\n");
        p.setHost (norns.address());
        CHECK (r.until ([&] { auto s = p.norns.snapshot (false); return s.status == Status::Connected && s.received == 47 && s.expected == 47; }),
               "connected with the list: %s", statusName (p.norns.status()));

        std::printf ("bind slot 1 to cutoff: the slot takes cutoff's value, its name follows\n");
        p.bindSlot (0, "cutoff", "cutoff");
        CHECK (std::abs (p.slotParam (0).getValue() - 0.25f) < 1e-4f, "slot took the value: %f", p.slotParam (0).getValue());
        CHECK (p.slotParam (0).getName (32) == "S1 cutoff", "%s", p.slotParam (0).getName (32).toRawUTF8());
        CHECK (p.slotOf ("cutoff") == 0 && p.slotId (0) == "cutoff", "lookup");
        r.run (150);
        CHECK (std::abs (simGet (norns, "cutoff") - 0.25f) < 1e-4f, "no jump on binding");

        std::printf ("host automation moves cutoff on norns\n");
        p.slotParam (0).setValueNotifyingHost (0.8f);
        CHECK (r.until ([&] { return std::abs (simGet (norns, "cutoff") - 0.8f) < 1e-3f; }), "cutoff = %f", simGet (norns, "cutoff"));
        CHECK (r.until ([&] { ParamInfo q; return p.norns.findParam ("cutoff", q) && std::abs (q.raw - 0.8f) < 1e-3f; }), "echo back");

        std::printf ("CONDUIT patches into SLOT1: knob + offset goes out\n");
        {
            fb::Bay bay;
            CHECK (bay.open ("CONDUIT"), "bay");
            bay.heartbeat();
            const int slot = p.link.getNode().slotIndex();
            bay.setMod (slot, p.link.port ("SLOT1"), -0.3f);
            CHECK (r.until ([&] { return std::abs (simGet (norns, "cutoff") - 0.5f) < 1e-3f; }), "cutoff = %f", simGet (norns, "cutoff"));
            CHECK (p.slotModulated (0), "modulated");
            CHECK (std::abs (p.slotParam (0).getValue() - 0.8f) < 1e-4f, "the knob stays");

            std::printf ("PRESS2 from the family presses K2 on norns\n");
            bay.trigger (slot, p.link.port ("PRESS2"), 1.0f);
            CHECK (r.until ([&] { auto l = norns.logs(); bool d = false, u = false; for (auto& x : l) { d |= x == "key 2 1"; u |= x == "key 2 0"; } return d && u; }), "K2 pressed and released");
            bay.close();
            CHECK (r.until ([&] { return std::abs (simGet (norns, "cutoff") - 0.8f) < 1e-3f; }), "bay gone, knob alone");
        }

        std::printf ("norns hardware out to the family\n");
        {
            fb::Hub hub;
            fb::Hub::Options o; o.epoch = false; o.dataScreen = false;
            CHECK (hub.open (o), "hub");
            hub.poll ([] (const fb::Event&) {});
            const int slot = p.link.getNode().slotIndex();
            float e2 = -1.0f;
            hub.value (slot, p.link.port ("E2"), 0.0, e2);
            CHECK (std::abs (e2 - 0.5f) < 1e-6f, "E2 rests at the middle: %f", e2);
            norns.cmd ("HW\tkey\t3\t1");
            norns.cmd ("HW\tenc\t2\t12");
            int k3 = 0;
            CHECK (r.until ([&]
            {
                hub.poll ([&] (const fb::Event& e) { if (e.type == fb::EvTrigger && e.a == 3 && e.port == (uint16_t) p.link.port ("K3")) ++k3; });
                float v = 0.0f;
                hub.value (slot, p.link.port ("E2"), 0.0, v);
                return k3 > 0 && std::abs (v - (0.5f + 12.0f / 96.0f)) < 1e-4f;
            }), "K3 event %d, E2 value", k3);
            CHECK (std::abs (p.live.e[1].load() - (0.5f + 12.0f / 96.0f)) < 1e-4f, "live E2");

            std::printf ("another script: SCRIPT event, slot names that are not in it\n");
            int data = 0;
            hub.poll ([] (const fb::Event&) {});
            norns.cmd ("SCRIPT\tmlr");
            CHECK (r.until ([&]
            {
                hub.poll ([&] (const fb::Event& e) { if (e.type == fb::EvDataLoaded && e.u == family::hash32 ("mlr")) ++data; });
                return data > 0;
            }), "SCRIPT event");
            CHECK (p.slotParam (0).getName (32) == "S1 cutoff", "keeps the stored name while cutoff is missing: %s", p.slotParam (0).getName (32).toRawUTF8());
        }

        std::printf ("PSET: a new number loads it, the same number does not reload\n");
        {
            auto* ps = p.apvts.getParameter (ids::pset);
            ps->setValueNotifyingHost (ps->convertTo0to1 (7.0f));
            CHECK (r.until ([&] { for (auto& l : norns.logs()) if (l == "pset_read 7") return true; return false; }), "pset 7 read");
            const auto before = norns.logs().size();
            r.run (200);
            CHECK (norns.logs().size() == before, "no reload");
        }

        std::printf ("clock: TEMPO + PLAY sends the host's tempo\n");
        {
            auto* sy = p.apvts.getParameter (ids::sync);
            sy->setValueNotifyingHost (sy->convertTo0to1 (2.0f));
            r.head.bpm = 172.5;
            CHECK (r.until ([&] { auto v = norns.cmd ("TEMPO"); return ! v.empty() && v[0] == "172.5 true"; }), "tempo");
        }

        std::printf ("state: host, bindings, theme come back\n");
        juce::MemoryBlock mb;
        p.slotParam (5).setValueNotifyingHost (0.4f);
        p.bindSlot (5, "lvl3", "level 3");
        p.apvts.state.setProperty ("theme", 3, nullptr);
        p.getStateInformation (mb);
        {
            Rig r2;
            r2.proc.setStateInformation (mb.getData(), (int) mb.getSize());
            CHECK (r2.proc.getHost() == juce::String (norns.address()), "host");
            CHECK (r2.proc.slotId (0) == "cutoff" && r2.proc.slotId (5) == "lvl3", "bindings");
            CHECK (r2.proc.slotParam (5).getName (32) == "S6 level 3", "%s", r2.proc.slotParam (5).getName (32).toRawUTF8());
            CHECK ((int) r2.proc.apvts.state.getProperty ("theme") == 3, "theme");
            CHECK (r2.proc.norns.slotBinding (5) == "lvl3", "link has the binding");
            r2.run (300);
            // the second instance is a second client; norns keeps answering both
            CHECK (r2.proc.norns.status() == Status::Connected, "second instance connects");
        }

        std::printf ("the editor: opens, streams the screen, closes\n");
        {
            norns.cmd ("PAINT\t3\t9");
            std::unique_ptr<juce::AudioProcessorEditor> ed (p.createEditor());
            auto* te = dynamic_cast<TetherEditor*> (ed.get());
            CHECK (te != nullptr, "editor");
            CHECK (r.until ([&] { te->pump (0.03f); return te->panel().hasFrame(); }), "a frame arrives");
            te->pump (0.03f);
            CHECK (te->list().visibleCount() == 46, "the list shows the script's rows: %d", te->list().visibleCount());
            for (int i = 0; i < ui::numThemes(); ++i) { te->setTheme (i); auto img = te->createComponentSnapshot (te->getLocalBounds()); CHECK (img.isValid(), "theme %d", i); }
            ed.reset();
        }

        std::printf ("screen: styles, SNAP, a GIF recording without an editor\n");
        {
            const auto media = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("tether_media_test");
            media.deleteRecursively();
            setenv ("TETHER_MEDIA_DIR", media.getFullPathName().toRawUTF8(), 1);
            p.setScreenStyle (5);
            CHECK (p.getScreenStyle() == 5, "style stored");
            p.setScreenStyle (99);
            CHECK (p.getScreenStyle() == 5, "style clamped: %d", p.getScreenStyle());
            norns.cmd ("PAINT\t0\t15");
            p.startRecording();
            CHECK (p.isRecording(), "recording");
            r.run (300);
            norns.cmd ("PAINT\t1\t8");
            r.run (300);
            norns.cmd ("PAINT\t2\t4");
            r.run (300);
            const auto png = p.snapshot();
            CHECK (png.existsAsFile(), "snapshot written");
            if (auto im = juce::ImageFileFormat::loadFrom (png); im.isValid())
            {
                CHECK (im.getWidth() == 512 && im.getHeight() == 256, "snapshot size %dx%d", im.getWidth(), im.getHeight());
                CHECK (im.getPixelAt (5, 5) == juce::Colours::black, "paper: level 15 is black");
                CHECK (im.getPixelAt (5, 200) == juce::Colours::white, "paper: level 0 is white");
            }
            else CHECK (false, "snapshot reads back");
            const auto gifFile = p.stopRecording();
            CHECK (! p.isRecording(), "stopped");
            CHECK (r.until ([&] { return ! p.encoding(); }, 5000), "encoded");
            CHECK (gifFile.existsAsFile() && gifFile == p.lastRecording(), "gif written: %s", gifFile.getFullPathName().toRawUTF8());
            juce::MemoryBlock mb2;
            gifFile.loadFileAsData (mb2);
            CHECK (mb2.getSize() > 100 && std::memcmp (mb2.getData(), "GIF89a", 6) == 0, "gif header");
            // the stream stops again with no editor and no recording (the mod is told fps 0)
            CHECK (gifFile.getFileName().startsWith ("tether_mlr_"), "named after the script: %s", gifFile.getFileName().toRawUTF8());
            std::printf ("  gif: %s (%d bytes)\n", gifFile.getFullPathName().toRawUTF8(), (int) mb2.getSize());
            p.setScreenStyle (0);
        }

        std::printf ("LINK off: the link goes quiet\n");
        {
            auto* lk = p.apvts.getParameter (ids::link);
            lk->setValueNotifyingHost (0.0f);
            CHECK (r.until ([&] { return p.norns.status() == Status::Off; }), "off");
            lk->setValueNotifyingHost (1.0f);
            CHECK (r.until ([&] { return p.norns.status() == Status::Connected; }), "back");
        }
    }

    norns.stop();
    std::printf ("processor: %d checks, %d failed\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
