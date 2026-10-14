// tether-audio: norns' audio to and from the TETHER plug-in, over UDP (docs/AUDIO.md).
//
// A JACK client with two inputs and two outputs:
//   in_1/in_2    fed by crone:output_1/2 (what norns plays out), sent to the plug-in
//   out_1/out_2  into crone:input_1/2 (beside the ADC), what the plug-in sends
// One plug-in at a time (the latest to subscribe). Started by the tether mod when a plug-in asks
// for audio; exits after two minutes without one. A second copy finds the port taken and exits.
//
//   tether-audio [--port 10112] [--idle 120] [--capture a,b] [--playback a,b] [--no-connect]
#include "JackApi.h"
#include "engine/AudioWire.h"
#include "engine/Udp.h"
#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdlib>
#include <cstring>
#include <string>
#include <thread>
#include <vector>

using namespace tether;
using namespace tether::audio;

namespace
{
    jackapi::Api jack;
    jackapi::client_t* client = nullptr;
    jackapi::port_t* inPort[2] {};
    jackapi::port_t* outPort[2] {};
    Sender toPlugin;           // norns -> plug-in
    Receiver fromPlugin;       // plug-in -> norns
    std::atomic<bool> running { true }, sendOn { false }, playOn { false };
    std::atomic<int> bufferMs { 40 };
    std::atomic<uint32_t> xruns { 0 };
    double jackRate = 48000.0;

    int process (jackapi::nframes_t n, void*)
    {
        const float* in[2] = { (const float*) jack.port_get_buffer (inPort[0], n), (const float*) jack.port_get_buffer (inPort[1], n) };
        float* out[2] = { (float*) jack.port_get_buffer (outPort[0], n), (float*) jack.port_get_buffer (outPort[1], n) };
        if (sendOn.load (std::memory_order_relaxed)) toPlugin.push (in, 2, (int) n, jackRate);
        static bool wasPlaying = false;
        const bool play = playOn.load (std::memory_order_relaxed);
        if (play && ! wasPlaying) fromPlugin.restart();
        wasPlaying = play;
        if (play) fromPlugin.pull (out, 2, (int) n, jackRate, bufferMs.load (std::memory_order_relaxed));
        else { std::memset (out[0], 0, n * sizeof (float)); std::memset (out[1], 0, n * sizeof (float)); }
        return 0;
    }
    int onXrun (void*) { xruns.fetch_add (1, std::memory_order_relaxed); return 0; }
    void onShutdown (void*) { running = false; }
    void onSignal (int) { running = false; }

    double now()
    {
        using namespace std::chrono;
        return duration<double> (steady_clock::now().time_since_epoch()).count();
    }

    std::vector<std::string> split (const std::string& s)
    {
        std::vector<std::string> out;
        size_t a = 0;
        while (a <= s.size())
        {
            const auto b = s.find (',', a);
            out.push_back (s.substr (a, b == std::string::npos ? std::string::npos : b - a));
            if (b == std::string::npos) break;
            a = b + 1;
        }
        return out;
    }

    // connect what is not connected yet; quiet about ports that do not exist (crone not up yet)
    int wire (const std::vector<std::string>& capture, const std::vector<std::string>& playback)
    {
        int made = 0;
        for (int c = 0; c < 2; ++c)
        {
            const std::string me_in = jack.port_name (inPort[c]), me_out = jack.port_name (outPort[c]);
            if (c < (int) capture.size() && ! capture[(size_t) c].empty() && jack.port_by_name (client, capture[(size_t) c].c_str()) != nullptr
                && ! jack.port_connected_to (inPort[c], capture[(size_t) c].c_str()))
                made += jack.connect (client, capture[(size_t) c].c_str(), me_in.c_str()) == 0;
            if (c < (int) playback.size() && ! playback[(size_t) c].empty() && jack.port_by_name (client, playback[(size_t) c].c_str()) != nullptr
                && ! jack.port_connected_to (outPort[c], playback[(size_t) c].c_str()))
                made += jack.connect (client, me_out.c_str(), playback[(size_t) c].c_str()) == 0;
        }
        return made;
    }
}

int main (int argc, char** argv)
{
    uint16_t port = kAudioPort;
    double idle = 120.0;
    bool autoConnect = true;
    std::vector<std::string> capture { "crone:output_1", "crone:output_2" }, playback { "crone:input_1", "crone:input_2" };
    std::string name = "tether";
    for (int i = 1; i < argc; ++i)
    {
        const std::string a = argv[i];
        auto next = [&] { return i + 1 < argc ? std::string (argv[++i]) : std::string(); };
        if (a == "--port") port = (uint16_t) std::atoi (next().c_str());
        else if (a == "--idle") idle = std::atof (next().c_str());
        else if (a == "--capture") capture = split (next());
        else if (a == "--playback") playback = split (next());
        else if (a == "--name") name = next();
        else if (a == "--no-connect") autoConnect = false;
        else if (a == "--version") { std::printf ("tether-audio %d\n", kVersion); return 0; }
        else { std::fprintf (stderr, "usage: tether-audio [--port N] [--idle S] [--capture a,b] [--playback a,b] [--no-connect]\n"); return 2; }
    }

    Udp sock;
    if (! sock.open (port)) { std::printf ("tether-audio: port %u is taken (already running?)\n", port); return 0; }
    if (! jack.load()) return 1;
    int status = 0;
    client = jack.client_open (name.c_str(), jackapi::NoStartServer, &status);
    if (client == nullptr) { std::fprintf (stderr, "tether-audio: no JACK server (status %d)\n", status); return 1; }
    const char* pn[4] = { "in_1", "in_2", "out_1", "out_2" };
    for (int c = 0; c < 2; ++c)
    {
        inPort[c] = jack.port_register (client, pn[c], jackapi::AudioType, jackapi::PortIsInput, 0);
        outPort[c] = jack.port_register (client, pn[2 + c], jackapi::AudioType, jackapi::PortIsOutput, 0);
        if (inPort[c] == nullptr || outPort[c] == nullptr) { std::fprintf (stderr, "tether-audio: cannot register ports\n"); return 1; }
    }
    jackRate = (double) jack.get_sample_rate (client);
    const int period = (int) jack.get_buffer_size (client);
    jack.set_process_callback (client, process, nullptr);
    jack.set_xrun_callback (client, onXrun, nullptr);
    jack.on_shutdown (client, onShutdown, nullptr);
    if (jack.activate (client) != 0) { std::fprintf (stderr, "tether-audio: cannot activate\n"); return 1; }
    std::signal (SIGINT, onSignal);
    std::signal (SIGTERM, onSignal);
    std::printf ("tether-audio %d: udp %u, jack %.0f Hz / %d, %s\n", kVersion, port, jackRate, period, name.c_str());
    std::fflush (stdout);
    if (autoConnect) wire (capture, playback);

    Address sub;                       // the plug-in
    SubMsg want;
    double lastSub = -1.0e9, lastStat = 0.0, lastWire = now(), lastSeen = now();
    uint8_t buf[65536], pkt[8192];
    const int chunk = std::min (period, 1024);

    while (running.load())
    {
        sockaddr_in from {};
        int n = sock.receive (buf, sizeof (buf), from, 1);
        const double t = now();
        for (int k = 0; n > 0 && k < 512; ++k)
        {
            Header h;
            SubMsg s;
            if (parseSub (buf, (size_t) n, s))
            {
                if (! sub.valid || sub.sa.sin_addr.s_addr != from.sin_addr.s_addr || sub.sa.sin_port != from.sin_port)
                {
                    std::printf ("tether-audio: plug-in at %s\n", Address { from, true }.text().c_str());
                    std::fflush (stdout);
                    toPlugin.flush();
                }
                sub.sa = from; sub.valid = true; want = s; lastSub = t;
                bufferMs.store (s.bufferMs);
            }
            else if (isControl (buf, (size_t) n) && buf[4] == Bye)
            {
                if (sub.valid && sub.sa.sin_addr.s_addr == from.sin_addr.s_addr && sub.sa.sin_port == from.sin_port) { sub.valid = false; lastSub = -1.0e9; }
            }
            else if (parseHeader (buf, (size_t) n, h) && sub.valid && sub.sa.sin_addr.s_addr == from.sin_addr.s_addr && want.toNorns)
                fromPlugin.push (h, buf + kHeader);
            n = sock.receive (buf, sizeof (buf), from, 0);
        }

        if (sub.valid && t - lastSub > 3.0) { sub.valid = false; std::printf ("tether-audio: plug-in gone\n"); std::fflush (stdout); }
        sendOn.store (sub.valid && want.fromNorns);
        playOn.store (sub.valid && want.toNorns);
        if (sub.valid) lastSeen = t;

        if (sub.valid && want.fromNorns)
        {
            size_t m;
            while ((m = toPlugin.pop (chunk, want.format, pkt)) > 0) sock.send (sub, pkt, m);
        }
        else toPlugin.flush();

        if (sub.valid && t - lastStat >= 0.5)
        {
            lastStat = t;
            StatMsg st;
            st.rate = (uint32_t) jackRate; st.period = (uint32_t) period; st.xruns = xruns.load();
            st.underruns = fromPlugin.underruns.load(); st.lost = fromPlugin.missing(); st.fillMs = (float) fromPlugin.fillMs();
            st.captureConnections = (uint8_t) (jack.port_connected (inPort[0]) + jack.port_connected (inPort[1]));
            st.playbackConnections = (uint8_t) (jack.port_connected (outPort[0]) + jack.port_connected (outPort[1]));
            st.packets = toPlugin.packets.load();
            const size_t m = encodeStat (st, pkt);
            sock.send (sub, pkt, m);
        }
        if (autoConnect && t - lastWire > 2.0) { lastWire = t; wire (capture, playback); }
        if (idle > 0.0 && t - lastSeen > idle) { std::printf ("tether-audio: idle, bye\n"); break; }
    }

    jack.deactivate (client);
    jack.client_close (client);
    return 0;
}
