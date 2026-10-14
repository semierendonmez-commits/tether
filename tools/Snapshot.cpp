// Renders the real editor to PNG without a display, against the stand-in norns (the real mod):
// a drawn norns screen, a few bound slots, every theme, and the states without a norns.
//   TetherSnapshot <out-dir>        (TETHER_TESTS_DIR=<repo>/tests if not run from the repo root)
#include <juce_audio_utils/juce_audio_utils.h>
#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "NornsSim.h"
#if defined(__linux__)
#include "JackRig.h"
#endif

using namespace tether;

// a screen that looks like a norns script: a title, a step sequence, a level meter, a value
static std::string demoScreen()
{
    juce::Image im (juce::Image::RGB, kScreenW, kScreenH, true);
    {
        juce::Graphics g (im);
        g.fillAll (juce::Colours::black);
        g.setColour (juce::Colour::greyLevel (1.0f));
        g.setFont (juce::Font (juce::FontOptions (9.0f)));
        g.drawText ("AWAKE", 2, 0, 60, 10, juce::Justification::centredLeft);
        g.setColour (juce::Colour::greyLevel (0.35f));
        g.drawText ("loop 1  16 steps", 50, 0, 76, 10, juce::Justification::centredRight);
        const int notes[16] = { 5, 0, 3, 0, 7, 5, 0, 2, 5, 0, 3, 6, 0, 7, 2, 0 };
        for (int i = 0; i < 16; ++i)
        {
            const int x = 2 + i * 8, h = notes[i] * 4;
            g.setColour (juce::Colour::greyLevel (i == 5 ? 1.0f : 0.45f));
            if (h > 0) g.fillRect (x, 44 - h, 6, h);
            g.setColour (juce::Colour::greyLevel (0.18f));
            g.fillRect (x, 46, 6, 1);
        }
        g.setColour (juce::Colour::greyLevel (0.6f));
        g.drawText ("cutoff  1.43 khz", 2, 52, 80, 11, juce::Justification::centredLeft);
        for (int i = 0; i < 20; ++i)
        {
            g.setColour (juce::Colour::greyLevel (i < 13 ? 0.25f + 0.035f * (float) i : 0.08f));
            g.fillRect (84 + i * 2, 55, 1, 6);
        }
    }
    std::string hex;
    hex.reserve (kScreenW * kScreenH);
    for (int y = 0; y < kScreenH; ++y)
        for (int x = 0; x < kScreenW; ++x)
        {
            const int lv = juce::jlimit (0, 15, juce::roundToInt (im.getPixelAt (x, y).getBrightness() * 15.0f));
            hex += "0123456789abcdef"[lv];
        }
    return hex;
}

static void save (juce::Component& c, const juce::File& f)
{
    const auto img = c.createComponentSnapshot (c.getLocalBounds(), true, 1.0f);
    f.deleteFile();
    juce::FileOutputStream os (f);
    juce::PNGImageFormat().writeImageToStream (img, os);
    std::printf ("%s\n", f.getFullPathName().toRawUTF8());
}

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI init;
    const juce::File out (juce::File::getCurrentWorkingDirectory().getChildFile (argc > 1 ? argv[1] : "screenshots"));
    out.createDirectory();

    // no norns at all first
    {
        TetherProcessor proc;
        proc.setHost ("127.0.0.1:9");
        std::unique_ptr<TetherEditor> ed (dynamic_cast<TetherEditor*> (proc.createEditor()));
        for (int i = 0; i < 100; ++i) { proc.tick(); ed->pump (1.0f / 30.0f); juce::Thread::sleep (40); }
        save (*ed, out.getChildFile ("no_norns.png"));
    }

    Bridge norns;
    if (! norns.start()) { std::printf ("no lua: only no_norns.png\n"); return 0; }
    norns.cmd ("PIXELS\t" + demoScreen());

    TetherProcessor proc;
    proc.setPlayConfigDetails (0, 2, 48000.0, 512);
    proc.prepareToPlay (48000.0, 512);
    proc.setHost (norns.address());
    juce::AudioBuffer<float> buf (2, 512);
    juce::MidiBuffer midi;
    std::unique_ptr<TetherEditor> ed (dynamic_cast<TetherEditor*> (proc.createEditor()));
    auto run = [&] (int frames)
    {
        for (int i = 0; i < frames; ++i) { proc.tick(); proc.processBlock (buf, midi); ed->pump (1.0f / 30.0f); juce::Thread::sleep (33); }
    };
    for (int i = 0; i < 150 && ! (proc.norns.snapshot (false).received == 47 && ed->panel().hasFrame()); ++i) run (1);

    proc.bindSlot (0, "cutoff", "cutoff");
    proc.bindSlot (1, "steps", "steps");
    proc.bindSlot (2, "scale", "scale");
    proc.bindSlot (3, "hold", "hold");
    for (int s = 4; s < 11; ++s) proc.bindSlot (s, "lvl" + juce::String (s - 3), "level " + juce::String (s - 3));
    proc.slotParam (0).setValueNotifyingHost (0.62f);
    proc.slotParam (2).setValueNotifyingHost (0.67f);
    for (int s = 4; s < 11; ++s) proc.slotParam (s).setValueNotifyingHost (0.15f + 0.1f * (float) (s - 4));
    norns.cmd ("HW\tkey\t2\t1");
    norns.cmd ("HW\tenc\t3\t5");
    run (20);

    save (*ed, out.getChildFile ("ui_full.png"));
    for (int i = 0; i < ui::numThemes(); ++i)
    {
        ed->setTheme (i);
        run (2);
        save (*ed, out.getChildFile ("theme_" + juce::String (i).paddedLeft ('0', 2) + "_" + juce::String (ui::theme (i).name).replaceCharacters (" '", "__") + ".png"));
    }
    ed->setTheme (0);
    for (int st = 0; st < 6; ++st)
    {
        proc.setScreenStyle (st);
        run (2);
        save (ed->panel(), out.getChildFile ("screen_" + juce::String (st) + ".png"));
    }
    proc.setScreenStyle (5);
    for (int th : { 0, 4 }) { ed->setTheme (th); run (2); save (*ed, out.getChildFile ("paper_theme_" + juce::String (th) + ".png")); }
    proc.setScreenStyle (1);
    ed->setTheme (0);
    proc.startRecording();
    run (10);
    save (*ed, out.getChildFile ("recording.png"));
    proc.stopRecording();

    // the AUDIO page; streaming when a daemon is given (Linux: tools/Snapshot <out> <tether-audio>)
    ed->setLinkPage (1);
    run (4);
    save (*ed, out.getChildFile ("audio_off.png"));
   #if defined(__linux__)
    if (argc > 2 && jackrig::start())
    {
        const std::string port = "31877";
        setenv ("TETHER_AUDIO_PORT", port.c_str(), 1);
        const pid_t d = jackrig::spawn ({ argv[2], "--port", port, "--idle", "20" });
        proc.setHost ("127.0.0.1:9");
        auto* f = proc.apvts.getParameter (ids::audioFrom);
        f->setValueNotifyingHost (1.0f);
        std::atomic<bool> go { true };
        std::thread host ([&]
        {
            juce::AudioBuffer<float> b (2, 256);
            juce::MidiBuffer m;
            auto next = std::chrono::steady_clock::now();
            while (go.load()) { proc.processBlock (b, m); next += std::chrono::microseconds (5805); std::this_thread::sleep_until (next); }
        });
        for (int i = 0; i < 90; ++i) { proc.tick(); ed->pump (1.0f / 30.0f); juce::Thread::sleep (33); }
        save (*ed, out.getChildFile ("audio_streaming.png"));
        go = false;
        host.join();
        jackrig::stopChild (d);
        jackrig::stop();
    }
   #endif
    ed->setLinkPage (0);
    ed.reset();
    norns.stop();
    return 0;
}
