// A JACK server (dummy driver, synchronous, 48 kHz / 128 as on norns) and a stand-in crone for the
// audio tests: crone:output_1/2 play 440 / 550 Hz, crone:input_1 counts zero crossings and peaks.
// Linux only (fork, the dummy driver).
#pragma once

#include "../norns/tether-audio/JackApi.h"
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <string>
#include <vector>
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>

namespace jackrig
{
inline jackapi::Api jack;
inline jackapi::port_t* cOut[2];
inline jackapi::port_t* cIn[2];
inline double phase[2];
inline std::atomic<int> zc { 0 }, samples { 0 };
inline std::atomic<float> peakIn { 0.0f };
inline float lastIn = 0.0f;

// the stand-in crone: plays 440 / 550 Hz, counts the zero crossings of what comes in on input_1
inline int croneProcess (jackapi::nframes_t n, void*)
{
    for (int c = 0; c < 2; ++c)
    {
        auto* o = (float*) jack.port_get_buffer (cOut[c], n);
        const double f = c == 0 ? 440.0 : 550.0;
        for (jackapi::nframes_t i = 0; i < n; ++i) { o[i] = 0.5f * (float) std::sin (phase[c]); phase[c] += 2.0 * M_PI * f / 48000.0; }
    }
    const auto* in = (const float*) jack.port_get_buffer (cIn[0], n);
    float pk = peakIn.load();
    for (jackapi::nframes_t i = 0; i < n; ++i)
    {
        if (lastIn < 0.0f && in[i] >= 0.0f) zc.fetch_add (1);
        lastIn = in[i];
        pk = std::max (pk, std::abs (in[i]));
    }
    peakIn.store (pk);
    samples.fetch_add ((int) n);
    return 0;
}

inline pid_t spawn (const std::vector<std::string>& args)
{
    const pid_t p = fork();
    if (p == 0)
    {
        std::vector<char*> a;
        for (auto& s : args) a.push_back (const_cast<char*> (s.c_str()));
        a.push_back (nullptr);
        execvp (a[0], a.data());
        _exit (127);
    }
    return p;
}

inline void stopChild (pid_t p) { if (p > 0) { kill (p, SIGTERM); waitpid (p, nullptr, 0); } }


inline pid_t jackd = -1;
inline jackapi::client_t* crone = nullptr;

// jackd + the stand-in crone; false (and nothing running) if jackd is missing
inline bool start()
{
    if (std::system ("command -v jackd >/dev/null 2>&1") != 0) return false;
    const std::string server = "tethertest" + std::to_string ((int) getpid());
    setenv ("JACK_DEFAULT_SERVER", server.c_str(), 1);
    setenv ("JACK_NO_AUDIO_RESERVATION", "1", 1);
    // synchronous, 48 kHz / 128: as norns runs it (norns-jack.service: jackd -R -P 95 -d alsa -r 48000 -n 3 -p 128 -S)
    jackd = spawn ({ "jackd", "-S", "--no-realtime", "-n", server, "-d", "dummy", "-r", "48000", "-p", "128" });
    usleep (800000);
    if (! jack.load()) return false;
    int status = 0;
    for (int i = 0; i < 30 && crone == nullptr; ++i) { crone = jack.client_open ("crone", jackapi::NoStartServer, &status); if (! crone) usleep (100000); }
    if (crone == nullptr) return false;
    cOut[0] = jack.port_register (crone, "output_1", jackapi::AudioType, jackapi::PortIsOutput, 0);
    cOut[1] = jack.port_register (crone, "output_2", jackapi::AudioType, jackapi::PortIsOutput, 0);
    cIn[0] = jack.port_register (crone, "input_1", jackapi::AudioType, jackapi::PortIsInput, 0);
    cIn[1] = jack.port_register (crone, "input_2", jackapi::AudioType, jackapi::PortIsInput, 0);
    jack.set_process_callback (crone, croneProcess, nullptr);
    jack.activate (crone);
    return true;
}

inline void stop()
{
    if (crone != nullptr) { jack.deactivate (crone); jack.client_close (crone); crone = nullptr; }
    stopChild (jackd);
    jackd = -1;
}

// the dummy driver is not a crystal: 1.0 = exactly 48 kHz against the wall clock
inline double clock (double secs)
{
    const int s0 = samples.load();
    const auto t0 = std::chrono::steady_clock::now();
    usleep ((useconds_t) (secs * 1.0e6));
    const double wall = std::chrono::duration<double> (std::chrono::steady_clock::now() - t0).count();
    return (samples.load() - s0) / wall / 48000.0;
}

// crone:input_1 over `secs`: frequency (by the dummy's own clock) and peak
inline double inputHz (double secs, float* peak = nullptr)
{
    zc = 0; peakIn = 0.0f;
    const int s0 = samples.load();
    usleep ((useconds_t) (secs * 1.0e6));
    if (peak != nullptr) *peak = peakIn.load();
    const int ns = samples.load() - s0;
    return ns > 0 ? zc.load() / (ns / 48000.0) : 0.0;
}

} // namespace jackrig
