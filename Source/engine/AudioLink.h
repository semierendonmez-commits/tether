// The plug-in's end of the audio stream: its own thread and socket, talking to tether-audio on
// norns (port 10112). No JUCE.
//   audio thread    process(): what to send in, what came from norns out
//   message thread  setTarget / setFromNorns / setToNorns / setBufferMs / setFormat, stats()
// Asks for the daemon (wantsDaemon) when it subscribes and hears nothing back: the processor
// passes that on to the mod (/tether/audio/start).
#pragma once

#include "AudioWire.h"
#include "Udp.h"
#include <chrono>
#include <mutex>
#include <string>
#include <thread>

namespace tether
{

class AudioLink
{
public:
    AudioLink() = default;
    ~AudioLink() { stop(); }

    void start()
    {
        if (running.exchange (true)) return;
        sock.open (0);
        th = std::thread ([this] { loop(); });
    }
    void stop()
    {
        if (! running.exchange (false)) return;
        if (th.joinable()) th.join();
        sock.close();
    }

    // ---- message thread --------------------------------------------------------------------------
    void setTarget (const std::string& ip, uint16_t port = audio::kAudioPort)
    {
        std::lock_guard<std::mutex> l (m);
        if (ip == targetIp && port == targetPort) return;
        targetIp = ip; targetPort = port;
        targetSerial.fetch_add (1);
    }
    void setFromNorns (bool b) noexcept { fromNorns.store (b); }
    void setToNorns (bool b) noexcept { toNorns.store (b); }
    void setBufferMs (int ms) noexcept { bufferMs.store (std::clamp (ms, 5, 500)); }
    void setFormat (uint8_t f) noexcept { format.store (f == audio::S16 ? audio::S16 : audio::S24); }
    int getBufferMs() const noexcept { return bufferMs.load(); }
    bool wantsDaemon() noexcept { return askDaemon.exchange (false); }

    struct Stats
    {
        bool daemon = false, playing = false;
        audio::StatMsg norns;            // what tether-audio reports
        double fillMs = 0.0, driftPpm = 0.0;
        uint32_t missing = 0, late = 0, underruns = 0, resyncs = 0, received = 0, sent = 0, dropped = 0;
        float inLevel = 0.0f, outL = 0.0f, outR = 0.0f;
    };
    Stats stats() const
    {
        Stats s;
        s.daemon = daemonUp.load();
        {
            std::lock_guard<std::mutex> l (m);
            s.norns = statMsg;
        }
        s.playing = rx.isPlaying(); s.fillMs = rx.fillMs(); s.driftPpm = rx.driftPpm();
        s.missing = rx.missing(); s.late = rx.late.load(); s.underruns = rx.underruns.load(); s.resyncs = rx.resyncs.load(); s.received = rx.received.load();
        s.sent = tx.packets.load(); s.dropped = tx.dropped.load();
        s.inLevel = rx.level(); s.outL = tx.meterL.load(); s.outR = tx.meterR.load();
        return s;
    }

    // ---- audio thread ----------------------------------------------------------------------------
    // what goes to norns (nullptr / 0 channels: silence)
    void send (const float* const* in, int inCh, int n, double sr, float gain) noexcept
    {
        if (toNorns.load (std::memory_order_relaxed) && daemonUp.load (std::memory_order_relaxed)) tx.push (in, inCh, n, sr, gain);
    }
    // what came from norns: added to out when `add` (an effect's dry signal stays), written otherwise
    void receive (float* const* out, int outCh, int n, double sr, float gain, bool add) noexcept
    {
        const bool from = fromNorns.load (std::memory_order_relaxed);
        if (from && ! wasFrom) rx.restart();
        wasFrom = from;
        if (from) rx.pull (out, outCh, n, sr, (double) bufferMs.load (std::memory_order_relaxed), gain, add);
        else if (! add) for (int c = 0; c < outCh; ++c) if (out[c] != nullptr) std::fill (out[c], out[c] + n, 0.0f);
    }
    void process (const float* const* in, int inCh, float* const* out, int outCh, int n, double sr,
                  float toGain, float fromGain, bool add) noexcept
    {
        send (in, inCh, n, sr, toGain);
        receive (out, outCh, n, sr, fromGain, add);
    }

    uint16_t localPort() const noexcept { return sock.port(); }

private:
    static double now()
    {
        using namespace std::chrono;
        return duration<double> (steady_clock::now().time_since_epoch()).count();
    }

    void loop()
    {
        uint8_t buf[65536], pkt[8192];
        Address target;
        uint32_t seenSerial = ~0u;
        double lastSub = -1.0e9, lastStat = -1.0e9, lastAsk = -1.0e9, subscribedAt = 0.0;
        bool subscribed = false;
        while (running.load())
        {
            const double t = now();
            if (targetSerial.load() != seenSerial)
            {
                seenSerial = targetSerial.load();
                if (subscribed && target.valid) { const size_t k = audio::encodeBye (pkt); sock.send (target, pkt, k); }
                std::string ip;
                uint16_t port;
                {
                    std::lock_guard<std::mutex> l (m);
                    ip = targetIp; port = targetPort;
                }
                target = ip.empty() ? Address {} : Udp::resolve (ip, port);   // an IP: no lookup, no wait
                subscribed = false;
                daemonUp.store (false);
            }

            const bool from = fromNorns.load(), to = toNorns.load(), want = (from || to) && target.valid;
            if (want && t - lastSub >= 0.5)
            {
                lastSub = t;
                if (! subscribed) subscribedAt = t;
                subscribed = true;
                audio::SubMsg s;
                s.fromNorns = from; s.toNorns = to; s.format = format.load(); s.bufferMs = (uint16_t) bufferMs.load();
                const size_t k = audio::encodeSub (s, pkt);
                sock.send (target, pkt, k);
            }
            else if (! want && subscribed)
            {
                if (target.valid) { const size_t k = audio::encodeBye (pkt); sock.send (target, pkt, k); }
                subscribed = false;
                daemonUp.store (false);
            }

            // nothing from tether-audio: ask the mod to start it (now and then)
            if (subscribed && t - lastStat > 1.5 && t - subscribedAt > 0.8 && t - lastAsk > 5.0) { lastAsk = t; askDaemon.store (true); }
            if (t - lastStat > 2.0) daemonUp.store (false);

            if (to && daemonUp.load())
            {
                size_t k;
                while ((k = tx.pop (chunkFor (rateHint), format.load(), pkt)) > 0) sock.send (target, pkt, k);
            }
            else tx.flush();

            sockaddr_in src {};
            int n = sock.receive (buf, sizeof (buf), src, 1);
            for (int k = 0; n > 0 && k < 512; ++k)
            {
                if (target.sameHost (src))
                {
                    audio::Header h;
                    audio::StatMsg st;
                    if (audio::parseHeader (buf, (size_t) n, h)) { if (from) rx.push (h, buf + audio::kHeader); }
                    else if (audio::parseStat (buf, (size_t) n, st))
                    {
                        lastStat = now();
                        daemonUp.store (true);
                        rateHint = st.rate;
                        std::lock_guard<std::mutex> l (m);
                        statMsg = st;
                    }
                }
                n = sock.receive (buf, sizeof (buf), src, 0);
            }
        }
        if (subscribed && target.valid) { const size_t k = audio::encodeBye (pkt); sock.send (target, pkt, k); }
    }

    // packets of about 2.7 ms: 128 frames at 48 kHz, 256 at 96 kHz
    static int chunkFor (uint32_t) { return 128; }

    std::thread th;
    std::atomic<bool> running { false };
    Udp sock;
    audio::Sender tx;
    audio::Receiver rx;
    mutable std::mutex m;
    std::string targetIp;
    uint16_t targetPort = audio::kAudioPort;
    std::atomic<uint32_t> targetSerial { 0 };
    std::atomic<bool> fromNorns { false }, toNorns { false }, daemonUp { false }, askDaemon { false };
    std::atomic<int> bufferMs { 40 };
    std::atomic<uint8_t> format { audio::S24 };
    audio::StatMsg statMsg;
    uint32_t rateHint = 48000;
    bool wasFrom = false;          // audio thread
};

} // namespace tether
