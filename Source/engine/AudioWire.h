// The audio half of TETHER: stereo audio over UDP between the plug-in and tether-audio on norns.
// Shared by both ends (no JUCE). docs/AUDIO.md has the protocol.
//
//   Sender    audio thread pushes frames, the network thread pops fixed-size packets
//   Receiver  the network thread pushes packets (lost ones become silence, late ones are dropped),
//             the audio thread pulls at its own sample rate through a jitter buffer and a 4-point
//             resampler whose ratio follows the buffer: the two ends' clocks never agree exactly,
//             so the reader runs a little faster or slower (at most 1 %) to keep the buffer at
//             its target. Underrun: silence, then the buffer fills again before playing.
// Neither side allocates, locks or makes a system call on the audio thread.
#pragma once

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <random>
#include <vector>

namespace tether::audio
{

constexpr uint16_t kAudioPort = 10112;          // tether-audio on norns
constexpr int kChannels = 2;
constexpr int kVersion = 1;
constexpr int kHeader = 24;
enum Format : uint8_t { S16 = 1, S24 = 2 };

// ---- little-endian helpers ----------------------------------------------------------------------
inline void put16 (uint8_t* p, uint16_t v) { p[0] = (uint8_t) v; p[1] = (uint8_t) (v >> 8); }
inline void put32 (uint8_t* p, uint32_t v) { for (int k = 0; k < 4; ++k) p[k] = (uint8_t) (v >> (8 * k)); }
inline uint16_t get16 (const uint8_t* p) { return (uint16_t) (p[0] | (p[1] << 8)); }
inline uint32_t get32 (const uint8_t* p) { return (uint32_t) p[0] | ((uint32_t) p[1] << 8) | ((uint32_t) p[2] << 16) | ((uint32_t) p[3] << 24); }
inline float getF (const uint8_t* p) { const uint32_t u = get32 (p); float f; std::memcpy (&f, &u, 4); return f; }
inline void putF (uint8_t* p, float f) { uint32_t u; std::memcpy (&u, &f, 4); put32 (p, u); }

// ---- audio packets --------------------------------------------------------------------------------
//   0 "TAU1"  4 stream id  8 seq  12 sample rate  16 frames (u16)  18 channels  19 format  20 reserved
struct Header
{
    uint32_t stream = 0, seq = 0, rate = 48000;
    uint16_t frames = 0;
    uint8_t channels = kChannels, format = S24;
};

inline int bytesPerSample (uint8_t f) { return f == S16 ? 2 : 3; }

inline bool isAudio (const uint8_t* d, size_t n) { return n >= (size_t) kHeader && std::memcmp (d, "TAU1", 4) == 0; }

inline bool parseHeader (const uint8_t* d, size_t n, Header& h)
{
    if (! isAudio (d, n)) return false;
    h.stream = get32 (d + 4); h.seq = get32 (d + 8); h.rate = get32 (d + 12);
    h.frames = get16 (d + 16); h.channels = d[18]; h.format = d[19];
    if (h.channels < 1 || h.channels > 8 || (h.format != S16 && h.format != S24) || h.rate < 8000 || h.rate > 384000) return false;
    return n >= (size_t) kHeader + (size_t) h.frames * h.channels * (size_t) bytesPerSample (h.format);
}

// interleaved stereo floats -> one packet; returns its size
inline size_t encode (const Header& h, const float* lr, uint8_t* out)
{
    std::memcpy (out, "TAU1", 4);
    put32 (out + 4, h.stream); put32 (out + 8, h.seq); put32 (out + 12, h.rate);
    put16 (out + 16, h.frames); out[18] = kChannels; out[19] = h.format; put32 (out + 20, 0);
    uint8_t* p = out + kHeader;
    const int n = h.frames * kChannels;
    for (int i = 0; i < n; ++i)
    {
        const float x = std::clamp (lr[i], -1.0f, 1.0f);
        if (h.format == S16)
        {
            const int v = (int) std::lrint (x * 32767.0f);
            p[0] = (uint8_t) v; p[1] = (uint8_t) (v >> 8); p += 2;
        }
        else
        {
            const int v = (int) std::lrint (x * 8388607.0f);
            p[0] = (uint8_t) v; p[1] = (uint8_t) (v >> 8); p[2] = (uint8_t) (v >> 16); p += 3;
        }
    }
    return (size_t) (p - out);
}

inline float sampleAt (const uint8_t* payload, uint8_t format, size_t index)
{
    if (format == S16)
    {
        const uint8_t* p = payload + index * 2;
        return (float) (int16_t) (uint16_t) (p[0] | (p[1] << 8)) / 32768.0f;
    }
    const uint8_t* p = payload + index * 3;
    int32_t v = p[0] | (p[1] << 8) | (p[2] << 16);
    if (v & 0x800000) v -= 0x1000000;
    return (float) v / 8388608.0f;
}

// ---- control packets ------------------------------------------------------------------------------
//   0 "TCT1"  4 kind
//   SUB  (plug-in -> norns, twice a second): 5 wantFromNorns  6 wantToNorns  7 format  8 bufferMs (u16)
//   STAT (norns -> plug-in, twice a second): 5 version  8 jack rate  12 period  16 xruns  20 underruns
//        24 lost  28 fill ms (f32)  32 capture connections  33 playback connections  36 packets sent
//   BYE  (plug-in -> norns): stop now
enum Kind : uint8_t { Sub = 1, Stat = 2, Bye = 3 };

struct SubMsg { bool fromNorns = false, toNorns = false; uint8_t format = S24; uint16_t bufferMs = 40; };
struct StatMsg
{
    uint8_t version = kVersion;
    uint32_t rate = 0, period = 0, xruns = 0, underruns = 0, lost = 0, packets = 0;
    float fillMs = 0.0f;
    uint8_t captureConnections = 0, playbackConnections = 0;
};

inline bool isControl (const uint8_t* d, size_t n) { return n >= 8 && std::memcmp (d, "TCT1", 4) == 0; }

inline size_t encodeSub (const SubMsg& s, uint8_t* o)
{
    std::memset (o, 0, 12);
    std::memcpy (o, "TCT1", 4); o[4] = Sub; o[5] = s.fromNorns; o[6] = s.toNorns; o[7] = s.format; put16 (o + 8, s.bufferMs);
    return 12;
}
inline size_t encodeBye (uint8_t* o) { std::memset (o, 0, 8); std::memcpy (o, "TCT1", 4); o[4] = Bye; return 8; }
inline size_t encodeStat (const StatMsg& s, uint8_t* o)
{
    std::memset (o, 0, 40);
    std::memcpy (o, "TCT1", 4); o[4] = Stat; o[5] = s.version;
    put32 (o + 8, s.rate); put32 (o + 12, s.period); put32 (o + 16, s.xruns); put32 (o + 20, s.underruns);
    put32 (o + 24, s.lost); putF (o + 28, s.fillMs); o[32] = s.captureConnections; o[33] = s.playbackConnections; put32 (o + 36, s.packets);
    return 40;
}
inline bool parseSub (const uint8_t* d, size_t n, SubMsg& s)
{
    if (! isControl (d, n) || d[4] != Sub || n < 12) return false;
    s.fromNorns = d[5] != 0; s.toNorns = d[6] != 0; s.format = d[7] == S16 ? S16 : S24; s.bufferMs = std::clamp<uint16_t> (get16 (d + 8), 5, 1000);
    return true;
}
inline bool parseStat (const uint8_t* d, size_t n, StatMsg& s)
{
    if (! isControl (d, n) || d[4] != Stat || n < 40) return false;
    s.version = d[5]; s.rate = get32 (d + 8); s.period = get32 (d + 12); s.xruns = get32 (d + 16); s.underruns = get32 (d + 20);
    s.lost = get32 (d + 24); s.fillMs = getF (d + 28); s.captureConnections = d[32]; s.playbackConnections = d[33]; s.packets = get32 (d + 36);
    return true;
}

// ---- Sender --------------------------------------------------------------------------------------
class Sender
{
public:
    static constexpr uint32_t kCap = 1u << 15;            // frames (0.68 s at 48 kHz)

    Sender() : buf ((size_t) kCap * kChannels, 0.0f)
    {
        std::random_device rd;
        stream = rd() | 1u;
    }

    // audio thread: n frames from up to two channels (one channel goes to both); nullptr = silence
    void push (const float* const* ch, int numCh, int n, double sampleRate, float gain = 1.0f) noexcept
    {
        rate.store ((uint32_t) std::lround (sampleRate), std::memory_order_relaxed);
        const uint64_t w0 = w.load (std::memory_order_relaxed);
        const uint64_t free = kCap - (w0 - r.load (std::memory_order_acquire));
        if ((uint64_t) n > free) { dropped.fetch_add (1, std::memory_order_relaxed); return; }
        peakL *= 0.9f; peakR *= 0.9f;
        for (int i = 0; i < n; ++i)
        {
            const size_t at = (size_t) ((w0 + (uint64_t) i) & (kCap - 1)) * kChannels;
            const float l = numCh > 0 && ch[0] != nullptr ? ch[0][i] * gain : 0.0f;
            const float rr = numCh > 1 && ch[1] != nullptr ? ch[1][i] * gain : l;
            buf[at] = l; buf[at + 1] = rr;
            peakL = std::max (peakL, std::abs (l)); peakR = std::max (peakR, std::abs (rr));
        }
        w.store (w0 + (uint64_t) n, std::memory_order_release);
        meterL.store (peakL, std::memory_order_relaxed); meterR.store (peakR, std::memory_order_relaxed);
    }

    // network thread: the next packet of `frames` frames, if that many are waiting
    size_t pop (int frames, uint8_t format, uint8_t* out) noexcept
    {
        const uint64_t r0 = r.load (std::memory_order_relaxed);
        if (w.load (std::memory_order_acquire) - r0 < (uint64_t) frames) return 0;
        float tmp[2048];
        frames = std::min (frames, 1024);
        for (int i = 0; i < frames; ++i)
        {
            const size_t at = (size_t) ((r0 + (uint64_t) i) & (kCap - 1)) * kChannels;
            tmp[2 * i] = buf[at]; tmp[2 * i + 1] = buf[at + 1];
        }
        r.store (r0 + (uint64_t) frames, std::memory_order_release);
        Header h;
        h.stream = stream; h.seq = ++seq; h.rate = rate.load (std::memory_order_relaxed);
        h.frames = (uint16_t) frames; h.format = format;
        packets.fetch_add (1, std::memory_order_relaxed);
        return encode (h, tmp, out);
    }

    // network thread: drop what is waiting (the stream was off)
    void flush() noexcept { r.store (w.load (std::memory_order_acquire), std::memory_order_release); }

    std::atomic<uint32_t> dropped { 0 }, packets { 0 };
    std::atomic<float> meterL { 0.0f }, meterR { 0.0f };

private:
    std::vector<float> buf;
    std::atomic<uint64_t> w { 0 }, r { 0 };
    std::atomic<uint32_t> rate { 48000 };
    uint32_t stream = 1, seq = 0;
    float peakL = 0.0f, peakR = 0.0f;
};

// ---- Receiver ------------------------------------------------------------------------------------
class Receiver
{
public:
    static constexpr uint32_t kCap = 1u << 16;            // frames (1.36 s at 48 kHz)
    static constexpr uint32_t kGuard = 8;

    Receiver() : buf ((size_t) kCap * kChannels, 0.0f) {}

    // network thread. A packet lands at its own place in the buffer (stream position from its
    // sequence number), so one that arrives out of order still fills its gap if it comes before the
    // reader gets there; a gap nobody fills plays as silence.
    void push (const Header& h, const uint8_t* payload) noexcept
    {
        const uint64_t w0 = w.load (std::memory_order_relaxed);
        const uint64_t r0 = r.load (std::memory_order_acquire);
        if (! haveSeq || h.stream != curStream || h.frames != fpp)
        {
            curStream = h.stream; fpp = h.frames; seqBase = h.seq; posBase = w0; haveSeq = true;
            streams.fetch_add (1, std::memory_order_relaxed);
        }
        int64_t pos = (int64_t) posBase + (int64_t) (int32_t) (h.seq - seqBase) * (int64_t) fpp;
        if (pos > (int64_t) w0 + (int64_t) (kCap / 4) || pos + (int64_t) kCap < (int64_t) w0)
        {
            // far away: the sender restarted or a long outage; carry on from here
            seqBase = h.seq; posBase = w0; pos = (int64_t) w0;
            resyncsIn.fetch_add (1, std::memory_order_relaxed);
        }
        if (pos < (int64_t) r0) { late.fetch_add (1, std::memory_order_relaxed); return; }   // its time has passed
        const uint64_t end = (uint64_t) pos + h.frames;
        if (end - r0 > kCap - kGuard) { overflow.fetch_add (1, std::memory_order_relaxed); return; }
        srcRate.store (h.rate, std::memory_order_relaxed);

        if ((uint64_t) pos > w0)                          // packets missing before this one: silence for now
        {
            for (uint64_t i = w0; i < (uint64_t) pos; ++i)
            {
                const size_t at = (size_t) (i & (kCap - 1)) * kChannels;
                buf[at] = buf[at + 1] = 0.0f;
            }
            lost.fetch_add ((uint32_t) (((uint64_t) pos - w0) / std::max<uint16_t> (1, fpp)), std::memory_order_relaxed);
        }
        else if ((uint64_t) pos < w0)
            recovered.fetch_add (1, std::memory_order_relaxed);

        for (int i = 0; i < h.frames; ++i)
        {
            const size_t at = (size_t) (((uint64_t) pos + (uint64_t) i) & (kCap - 1)) * kChannels;
            const size_t s = (size_t) i * h.channels;
            buf[at] = sampleAt (payload, h.format, s);
            buf[at + 1] = h.channels > 1 ? sampleAt (payload, h.format, s + 1) : buf[at];
        }
        if (end > w0) w.store (end, std::memory_order_release);
        received.fetch_add (1, std::memory_order_relaxed);
    }

    // packets that never arrived (the ones that came late but in time are not counted)
    uint32_t missing() const noexcept
    {
        const uint32_t l = lost.load (std::memory_order_relaxed), rc = recovered.load (std::memory_order_relaxed);
        return l > rc ? l - rc : 0;
    }

    // audio thread: n frames at dstRate, added to out (or written when `add` is false)
    void pull (float* const* out, int numCh, int n, double dstRate, double targetMs, float gain = 1.0f, bool add = false) noexcept
    {
        const double sr = (double) srcRate.load (std::memory_order_relaxed);
        const double target = std::max (64.0, targetMs * sr / 1000.0);
        const uint64_t wNow = w.load (std::memory_order_acquire);
        double avail = (double) (wNow - rd) - frac;

        auto silence = [&] (int from)
        {
            if (! add) for (int c = 0; c < numCh; ++c) if (out[c] != nullptr) std::fill (out[c] + from, out[c] + n, 0.0f);
        };

        if (! playing)
        {
            if (avail < target) { silence (0); publish(); return; }
            playing = true;
            if (avail > target * 1.25) { rd = wNow - (uint64_t) target; frac = 0.0; avail = target; }
            fillAvg = avail;
            integ = 0.0;
        }
        // far too much waiting (a burst after a stall, or the stream came back): jump to the target
        if (avail > std::max (2.0 * target, target + sr * 0.15))
        {
            rd = wNow - (uint64_t) target; frac = 0.0; avail = target; fillAvg = target;
            resyncs.fetch_add (1, std::memory_order_relaxed);
        }

        // drift control in seconds of buffer: settles in a few seconds, smooth enough that network
        // jitter does not wobble the pitch (a 10 ms burst moves the ratio by well under 0.1 %)
        const double blockSec = n / dstRate;
        fillAvg += (avail - fillAvg) * std::min (1.0, blockSec / 1.0);
        const double err = std::clamp ((fillAvg - target) / sr, -0.2, 0.2);
        integ = std::clamp (integ + err * blockSec, -0.6, 0.6);
        adj = std::clamp (0.2 * err + 0.016 * integ, -0.01, 0.01);
        const double step = sr / dstRate * (1.0 + adj);

        float pk = 0.0f;
        for (int i = 0; i < n; ++i)
        {
            if (wNow - rd < 3) { silence (i); playing = false; underruns.fetch_add (1, std::memory_order_relaxed); break; }
            const float t = (float) frac;
            float o[2];
            for (int c = 0; c < 2; ++c)
            {
                const float y0 = at (rd - 1, c), y1 = at (rd, c), y2 = at (rd + 1, c), y3 = at (rd + 2, c);
                // 4-point, 3rd-order Hermite
                const float c1 = 0.5f * (y2 - y0), c2 = y0 - 2.5f * y1 + 2.0f * y2 - 0.5f * y3, c3 = 0.5f * (y3 - y0) + 1.5f * (y1 - y2);
                o[c] = (((c3 * t + c2) * t + c1) * t + y1) * gain;
            }
            pk = std::max (pk, std::max (std::abs (o[0]), std::abs (o[1])));
            for (int c = 0; c < numCh; ++c)
                if (out[c] != nullptr) { if (add) out[c][i] += o[std::min (c, 1)]; else out[c][i] = o[std::min (c, 1)]; }
            frac += step;
            const double whole = std::floor (frac);
            rd += (uint64_t) whole;
            frac -= whole;
        }
        meter = std::max (pk, meter * 0.9f);
        publish();
    }

    // audio thread: start over (the stream was off): drop what is waiting, fill the buffer again first
    void restart() noexcept
    {
        rd = w.load (std::memory_order_acquire);
        frac = 0.0; integ = 0.0; adj = 0.0;
        playing = false;
        publish();
    }

    // stats (any thread)
    double fillMs() const noexcept { return fillMsNow.load (std::memory_order_relaxed); }
    double driftPpm() const noexcept { return adjNow.load (std::memory_order_relaxed) * 1.0e6; }
    bool isPlaying() const noexcept { return playingNow.load (std::memory_order_relaxed); }
    float level() const noexcept { return meterNow.load (std::memory_order_relaxed); }
    uint32_t rate() const noexcept { return srcRate.load (std::memory_order_relaxed); }

    std::atomic<uint32_t> received { 0 }, lost { 0 }, recovered { 0 }, late { 0 }, overflow { 0 }, underruns { 0 }, resyncs { 0 }, resyncsIn { 0 }, streams { 0 };

private:
    float at (uint64_t i, int c) const noexcept { return buf[(size_t) (i & (kCap - 1)) * kChannels + (size_t) c]; }
    void publish() noexcept
    {
        r.store (rd, std::memory_order_release);           // the next frame to read (rd - 1 is still read: kGuard covers it)
        const double sr = (double) srcRate.load (std::memory_order_relaxed);
        fillMsNow.store ((double) (w.load (std::memory_order_relaxed) - rd) * 1000.0 / sr, std::memory_order_relaxed);
        adjNow.store (adj, std::memory_order_relaxed);
        playingNow.store (playing, std::memory_order_relaxed);
        meterNow.store (meter, std::memory_order_relaxed);
    }

    std::vector<float> buf;
    std::atomic<uint64_t> w { 0 }, r { 0 };
    std::atomic<uint32_t> srcRate { 48000 };
    // network thread
    uint32_t curStream = 0, seqBase = 0;
    uint16_t fpp = 0;
    uint64_t posBase = 0;
    bool haveSeq = false;
    // audio thread
    uint64_t rd = 0;
    double frac = 0.0, fillAvg = 0.0, integ = 0.0, adj = 0.0;
    bool playing = false;
    float meter = 0.0f;
    std::atomic<double> fillMsNow { 0.0 }, adjNow { 0.0 };
    std::atomic<bool> playingNow { false };
    std::atomic<float> meterNow { 0.0f };
};

} // namespace tether::audio
