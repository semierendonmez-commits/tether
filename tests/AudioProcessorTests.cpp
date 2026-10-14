// The audio stream through the processor, built twice: TETHER (instrument) and TETHER FX (effect).
// A real JACK server, the stand-in crone and tether-audio (tests/JackRig.h); the processor finds
// norns through NornsLink (127.0.0.1) and streams with it, with a 44.1 kHz "host" in this test.
//   TetherAudio{Synth,Fx}Tests <path to tether-audio>
#include <juce_audio_utils/juce_audio_utils.h>
#include "PluginProcessor.h"
#include "JackRig.h"

static int checks = 0, failures = 0;
#define CHECK(cond, ...) do { ++checks; if (! (cond)) { ++failures; std::printf ("FAIL %s:%d  %s  ", __FILE__, __LINE__, #cond); std::printf (__VA_ARGS__); std::printf ("\n"); } } while (0)

using namespace jackrig;

static void setParam (TetherProcessor& p, const char* id, float plain)
{
    auto* prm = p.apvts.getParameter (id);
    prm->setValueNotifyingHost (prm->convertTo0to1 (plain));
}

// runs the processor in real time at 44.1 kHz / 256: the input is a 660 Hz sine (FX), the output kept
struct Host
{
    TetherProcessor& p;
    std::atomic<bool> run { true }, keep { false };
    std::vector<float> outL;
    std::mutex m;
    std::thread th;
    double inPhase = 0.0;
    float inAmp = 0.5f;
    explicit Host (TetherProcessor& proc) : p (proc)
    {
        th = std::thread ([this]
        {
            constexpr int n = 256;
            juce::AudioBuffer<float> buf (2, n);
            juce::MidiBuffer midi;
            auto next = std::chrono::steady_clock::now();
            while (run.load())
            {
                buf.clear();
                if (TetherProcessor::isFx)
                    for (int i = 0; i < n; ++i)
                    {
                        const float v = inAmp * (float) std::sin (inPhase);
                        inPhase += 2.0 * M_PI * 660.0 / 44100.0;
                        buf.setSample (0, i, v); buf.setSample (1, i, v);
                    }
                p.processBlock (buf, midi);
                if (keep.load()) { std::lock_guard<std::mutex> g (m); outL.insert (outL.end(), buf.getReadPointer (0), buf.getReadPointer (0) + n); }
                next += std::chrono::microseconds ((int) (n * 1.0e6 / 44100.0));
                std::this_thread::sleep_until (next);
            }
        });
    }
    ~Host() { run = false; th.join(); }
    std::vector<float> record (double secs)
    {
        { std::lock_guard<std::mutex> g (m); outL.clear(); }
        keep = true;
        std::this_thread::sleep_for (std::chrono::milliseconds ((int) (secs * 1000)));
        keep = false;
        std::lock_guard<std::mutex> g (m);
        return outL;
    }
};

// the strength of one frequency in a recording (Goertzel), relative to a full-scale sine
static double tone (const std::vector<float>& x, double hz, double sr = 44100.0)
{
    if (x.empty()) return 0.0;
    const double w = 2.0 * M_PI * hz / sr, c = 2.0 * std::cos (w);
    double s1 = 0.0, s2 = 0.0;
    for (float v : x) { const double s0 = v + c * s1 - s2; s2 = s1; s1 = s0; }
    return 2.0 * std::sqrt (std::max (0.0, s1 * s1 + s2 * s2 - c * s1 * s2)) / (double) x.size();
}

// the dummy JACK clock is off by up to a few tenths of a percent, so norns' tones arrive a little
// sharp or flat: the strongest bin within 2 % of the frequency
static double toneNear (const std::vector<float>& x, double hz)
{
    double best = 0.0;
    for (double f = hz * 0.98; f <= hz * 1.02; f += 0.2) best = std::max (best, tone (x, f));
    return best;
}

static void pump (TetherProcessor& p, double secs)
{
    for (int i = 0; i < (int) (secs * 100); ++i) { p.tick(); juce::Thread::sleep (10); }
}

int main (int argc, char** argv)
{
    const char* kind = TetherProcessor::isFx ? "TETHER FX" : "TETHER";
    if (argc < 2) return 2;
    juce::ScopedJuceInitialiser_GUI init;
    ::unlink ("/dev/shm/semi.family.v1");
    ::unlink ("/dev/shm/datascreen.bus.1");
    if (! jackrig::start()) { std::printf ("no jackd: %s audio test skipped\n", kind); jackrig::stop(); return 0; }
    const std::string port = std::to_string (30000 + getpid() % 20000);
    setenv ("TETHER_AUDIO_PORT", port.c_str(), 1);
    const pid_t daemon = spawn ({ argv[1], "--port", port, "--idle", "30" });
    usleep (300000);
    std::printf ("%s\n", kind);

    {
        TetherProcessor p;
        p.setPlayConfigDetails (TetherProcessor::isFx ? 2 : 0, 2, 44100.0, 256);
        p.prepareToPlay (44100.0, 256);
        CHECK (p.getTotalNumInputChannels() == (TetherProcessor::isFx ? 2 : 0), "inputs %d", p.getTotalNumInputChannels());
        p.setHost ("127.0.0.1:9");                       // norns' address; no mod needed for audio
        pump (p, 0.3);
        CHECK (p.getLatencySamples() == 0, "no latency while audio is off");
        Host host (p);

        std::printf ("audio off: %s\n", TetherProcessor::isFx ? "the input passes" : "silence");
        auto x = host.record (0.5);
        if (TetherProcessor::isFx) CHECK (tone (x, 660) > 0.4, "dry %f", tone (x, 660));
        else CHECK (toneNear (x, 440) < 0.001 && tone (x, 660) < 0.001, "silent");

        std::printf ("from norns on: 440 Hz arrives, latency = the buffer\n");
        setParam (p, ids::audioFrom, 1.0f);
        pump (p, 2.5);
        CHECK (p.getLatencySamples() == juce::roundToInt (40.0 * 44100.0 / 1000.0), "latency %d", p.getLatencySamples());
        // a busy container's dummy clock can drop the odd cycle: the best of three windows
        auto best440 = [&] (double secs) { double b = 0.0; for (int k = 0; k < 3 && b < 0.4; ++k) { x = host.record (secs); b = std::max (b, toneNear (x, 440)); } return b; };
        const double t440 = best440 (1.5);
        std::printf ("  440 Hz %.3f  660 Hz %.3f\n", t440, tone (x, 660));
        CHECK (t440 > 0.35, "norns' 440 Hz: %f", t440);
        if (TetherProcessor::isFx) CHECK (tone (x, 660) > 0.4, "dry still there: %f", tone (x, 660));
        auto st = p.audio.stats();
        CHECK (st.daemon && st.norns.captureConnections == 2, "daemon %d wired %d", (int) st.daemon, st.norns.captureConnections);

        std::printf ("level -12 dB\n");
        setParam (p, ids::audioFromGain, -12.0f);
        pump (p, 0.2);
        const double t12 = best440 (1.0) == 0.0 ? 0.0 : toneNear (x, 440);
        double lv = t12;
        for (int k = 0; k < 3 && std::abs (lv / 0.5 - 0.251) >= 0.03; ++k) { x = host.record (1.0); lv = toneNear (x, 440); }
        CHECK (std::abs (lv / 0.5 - 0.251) < 0.03, "-12 dB of the 0.5 sine: %f", lv / 0.5);
        setParam (p, ids::audioFromGain, 0.0f);

        if (TetherProcessor::isFx)
        {
            std::printf ("to norns on: the 660 Hz input reaches crone:input_1\n");
            setParam (p, ids::audioTo, 1.0f);
            pump (p, 1.5);
            float pk = 0.0f;
            const double hz = jackrig::inputHz (2.0, &pk);
            std::printf ("  crone:input_1 %.1f Hz, peak %.2f\n", hz, pk);
            CHECK (pk > 0.4f && hz > 640.0 && hz < 680.0, "660 Hz at crone");

            std::printf ("THRU off: only norns\n");
            setParam (p, ids::audioThru, 0.0f);
            pump (p, 0.3);
            x = host.record (1.0);
            CHECK (tone (x, 660) < 0.01 && toneNear (x, 440) > 0.35, "660 %f 440 %f", tone (x, 660), toneNear (x, 440));

            std::printf ("THRU on, from norns off: the dry signal again, no latency\n");
            setParam (p, ids::audioThru, 1.0f);
            setParam (p, ids::audioFrom, 0.0f);
            pump (p, 0.3);
            CHECK (p.getLatencySamples() == 0, "latency %d", p.getLatencySamples());
            x = host.record (0.8);
            CHECK (tone (x, 660) > 0.4 && toneNear (x, 440) < 0.01, "660 %f 440 %f", tone (x, 660), toneNear (x, 440));
            setParam (p, ids::audioTo, 0.0f);
            pump (p, 0.5);
            const double hz2 = jackrig::inputHz (0.6, &pk);
            CHECK (pk == 0.0f, "to norns off: crone hears nothing (%f, %f Hz)", pk, hz2);
        }
        else
        {
            std::printf ("TETHER cannot send: to norns stays off\n");
            setParam (p, ids::audioTo, 1.0f);
            pump (p, 1.0);
            float pk = 1.0f;
            jackrig::inputHz (0.6, &pk);
            CHECK (pk == 0.0f, "the instrument sends nothing: %f", pk);
        }

        std::printf ("LINK off: silence (%s)\n", TetherProcessor::isFx ? "and the dry signal" : "");
        setParam (p, ids::audioFrom, 1.0f);
        setParam (p, ids::link, 0.0f);
        pump (p, 0.5);
        x = host.record (0.6);
        CHECK (toneNear (x, 440) < 0.01, "no norns: %f", toneNear (x, 440));
    }

    stopChild (daemon);
    jackrig::stop();
    std::printf ("%s audio: %d checks, %d failed\n", kind, checks, failures);
    return failures == 0 ? 0 : 1;
}
