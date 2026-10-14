// Audio end to end, Linux: a real JACK server (dummy driver, 48 kHz / 128 as on norns), a stand-in
// crone (this process: output_1/2 play 440 / 550 Hz, input_1/2 measure what arrives), the real
// tether-audio daemon as a child process, and the plug-in's AudioLink running a 44.1 kHz "host"
// that sends 660 Hz. Checks both directions by frequency and by clicks, the daemon request when
// nothing answers, the stats, and that turning a direction off stops it.
//   TetherAudioE2E <path to tether-audio>        (skipped when jackd is not installed)
#include "engine/AudioLink.h"
#include "JackRig.h"
#include <cstdio>
#include <cstdlib>
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>

static int checks = 0, failures = 0;
#define CHECK(cond, ...) do { ++checks; if (! (cond)) { ++failures; std::printf ("FAIL %s:%d  %s  ", __FILE__, __LINE__, #cond); std::printf (__VA_ARGS__); std::printf ("\n"); } } while (0)

using namespace tether;
using namespace jackrig;

int main (int argc, char** argv)
{
    if (argc < 2) { std::printf ("usage: TetherAudioE2E <tether-audio>\n"); return 2; }
    if (! jackrig::start()) { std::printf ("no jackd: audio end-to-end skipped\n"); jackrig::stop(); return 0; }

    // the plug-in side, with a 44.1 kHz host on its own thread
    const uint16_t port = (uint16_t) (20000 + getpid() % 20000);
    AudioLink link;
    link.start();
    link.setTarget ("127.0.0.1", port);
    link.setBufferMs (40);
    std::atomic<bool> hostRun { true };
    std::vector<float> got;
    std::mutex gotM;
    std::atomic<bool> collect { false };
    std::thread host ([&]
    {
        constexpr int n = 256;
        constexpr double sr = 44100.0;
        float a[n], l[n], r[n];
        double ph = 0.0;
        auto next = std::chrono::steady_clock::now();
        while (hostRun.load())
        {
            for (int i = 0; i < n; ++i) { a[i] = 0.5f * (float) std::sin (ph); ph += 2.0 * M_PI * 660.0 / sr; }
            const float* in[1] = { a };
            float* out[2] = { l, r };
            link.process (in, 1, out, 2, n, sr, 1.0f, 1.0f, false);
            if (collect.load()) { std::lock_guard<std::mutex> g (gotM); for (int i = 0; i < n; ++i) { got.push_back (l[i]); got.push_back (r[i]); } }
            next += std::chrono::microseconds ((int) (n * 1.0e6 / sr));
            std::this_thread::sleep_until (next);
        }
    });

    std::printf ("no daemon yet: the link asks for one\n");
    link.setFromNorns (true);
    link.setToNorns (true);
    bool asked = false;
    for (int i = 0; i < 40 && ! asked; ++i) { usleep (100000); asked = link.wantsDaemon(); }
    CHECK (asked, "wantsDaemon");
    CHECK (! link.stats().daemon, "no daemon seen");

    std::printf ("the daemon: wires itself to crone, answers\n");
    const pid_t daemon = spawn ({ argv[1], "--port", std::to_string (port), "--idle", "20" });
    bool up = false;
    for (int i = 0; i < 50 && ! up; ++i) { usleep (100000); up = link.stats().daemon; }
    CHECK (up, "daemon up");
    usleep (1500000);
    auto st = link.stats();
    CHECK (st.norns.rate == 48000 && st.norns.period == 128, "jack %u / %u", st.norns.rate, st.norns.period);
    CHECK (st.norns.captureConnections == 2 && st.norns.playbackConnections == 2, "wired %u %u", st.norns.captureConnections, st.norns.playbackConnections);

    // the dummy driver is not a crystal: in a busy container it runs fast or slow by up to a few
    // tenths of a percent. Measure it and expect the frequencies it implies (norns' own clock
    // drifts by parts per million; the clean-signal checks live in the engine test's simulation).
    std::printf ("norns -> plug-in: 440 / 550 Hz at 44.1 kHz\n");
    { std::lock_guard<std::mutex> g (gotM); got.clear(); }
    collect = true;
    const double speed = jackrig::clock (4.0);
    collect = false;
    std::printf ("  the dummy JACK clock runs at %.4f x\n", speed);
    const bool steady = std::abs (speed - 1.0) < 0.005;      // a container this busy measures the rig, not the code
    if (! steady) std::printf ("  (the dummy clock is too unsteady for the frequency checks this run)\n");
    {
        std::lock_guard<std::mutex> g (gotM);
        int zl = 0, zr = 0;
        float maxStep = 0.0f;
        for (size_t i = 2; i < got.size(); i += 2)
        {
            if (got[i - 2] < 0.0f && got[i] >= 0.0f) ++zl;
            if (got[i - 1] < 0.0f && got[i + 1] >= 0.0f) ++zr;
            maxStep = std::max (maxStep, std::abs (got[i] - got[i - 2]));
        }
        const double secs = (double) got.size() / 2.0 / 44100.0;
        CHECK (zl / secs > 400.0 && zl / secs < 480.0 && zr / secs > 500.0 && zr / secs < 600.0, "norns' 440 / 550 Hz reach the plug-in");
        std::printf ("  L %.1f Hz  R %.1f Hz  max step %.4f (a clean 550 Hz: %.4f)\n", zl / secs, zr / secs, maxStep, 0.5 * 2 * M_PI * 440 / 44100);
        if (steady) CHECK (std::abs (zl / secs - 440.0 * speed) < 440.0 * 0.004 + 1.0, "L %f (expected %f)", zl / secs, 440.0 * speed);
        if (steady) CHECK (std::abs (zr / secs - 550.0 * speed) < 550.0 * 0.004 + 1.0, "R %f (expected %f)", zr / secs, 550.0 * speed);
        if (std::abs (speed - 1.0) < 0.002) CHECK (maxStep < 0.045f, "clicks: %f", maxStep);
    }
    st = link.stats();
    std::printf ("  fill %.1f ms  drift %.0f ppm  missing %u  late %u  underruns %u  resyncs %u\n", st.fillMs, st.driftPpm, st.missing, st.late, st.underruns, st.resyncs);
    if (std::abs (speed - 1.0) < 0.002) CHECK (st.underruns <= 1, "underruns %u", st.underruns);
    CHECK (st.fillMs > 5.0 && st.fillMs < 80.0, "fill %f", st.fillMs);

    std::printf ("plug-in -> norns: 660 Hz arrives on crone:input_1\n");
    zc = 0;
    const int sIn0 = samples.load();
    const double speed2 = jackrig::clock (3.0);
    const double inHz = zc.load() / ((samples.load() - sIn0) / 48000.0);
    std::printf ("  crone:input_1 %.1f Hz, peak %.2f\n", inHz, peakIn.load());
    CHECK (peakIn.load() > 0.4f && inHz > 600.0 && inHz < 720.0, "the plug-in's 660 Hz reaches crone");
    if (steady && std::abs (speed2 - 1.0) < 0.005) CHECK (std::abs (inHz - 660.0 / speed2) < 660.0 * 0.004 + 1.0, "in %f (expected %f)", inHz, 660.0 / speed2);
    st = link.stats();
    CHECK (st.norns.fillMs > 5.0f, "norns side: underruns %u fill %.1f", st.norns.underruns, st.norns.fillMs);

    std::printf ("directions off: silence both ways\n");
    link.setToNorns (false);
    link.setFromNorns (false);
    usleep (1000000);
    peakIn = 0.0f;
    { std::lock_guard<std::mutex> g (gotM); got.clear(); }
    collect = true;
    usleep (800000);
    collect = false;
    float mx = 0.0f;
    { std::lock_guard<std::mutex> g (gotM); for (float v : got) mx = std::max (mx, std::abs (v)); }
    CHECK (mx == 0.0f, "plug-in output silent: %f", mx);
    CHECK (peakIn.load() == 0.0f, "crone input silent: %f", peakIn.load());

    std::printf ("16-bit, back on: still 440 Hz\n");
    link.setFormat (audio::S16);
    link.setFromNorns (true);
    usleep (1500000);
    { std::lock_guard<std::mutex> g (gotM); got.clear(); }
    collect = true;
    usleep (2000000);
    collect = false;
    {
        std::lock_guard<std::mutex> g (gotM);
        int zl = 0;
        for (size_t i = 2; i < got.size(); i += 2) if (got[i - 2] < 0.0f && got[i] >= 0.0f) ++zl;
        const double secs = (double) got.size() / 2.0 / 44100.0;
        if (steady) CHECK (std::abs (zl / secs - 440.0 * speed) < 440.0 * 0.006 + 1.0, "16-bit L %f", zl / secs);
    }

    hostRun = false;
    host.join();
    link.stop();
    stopChild (daemon);
    jackrig::stop();
    std::printf ("audio e2e: %d checks, %d failed\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
