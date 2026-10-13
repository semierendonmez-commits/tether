// The link to one norns: a thread that owns a UDP socket and speaks the tether mod's protocol
// (docs/PROTOCOL.md). Everything the plug-in shows comes out of here as copies; everything it
// sends goes in through a lock-free path (audio thread) or a small queue (message thread). No JUCE.
//
//   audio thread    setSlot / setClock / pressKey / popHw          (atomics, SPSC ring: no locks)
//   message thread  host, commands, bindings, snapshot, screen      (short mutexes, never held long)
//   link thread     resolve (in a detached helper), hello / ping, dump, slot sends, screen bands
#pragma once

#include "Osc.h"
#include "Udp.h"
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

namespace tether
{

constexpr int kSlots = 32;
constexpr int kScreenW = 128, kScreenH = 64, kBands = 8, kBandRows = 8;
constexpr uint16_t kNornsPort = 10111;
constexpr int kProtocol = 1;

enum ParamType { TNumber = 1, TOption = 2, TControl = 3, TFile = 4, TTaper = 5, TTrigger = 6, TGroup = 7, TText = 8, TBinary = 9 };

struct ParamInfo
{
    int index = 0;              // the mod's row number, 1-based
    std::string id, name, text;
    int type = 0, steps = 0;    // steps: positions of a stepped parameter, 0 continuous
    float raw = 0.0f;           // 0..1
    bool hidden = false, known = false;
};

enum class Status
{
    Off,            // disabled
    Resolving,      // looking up the host name
    NotFound,       // the name did not resolve
    Waiting,        // hello sent, nothing back yet
    NoMod,          // nothing back for a while: is the tether mod on? (keys / encoders still go out)
    Connected,
    Lost            // it was there and went quiet
};

const char* statusName (Status) noexcept;

// a key or an encoder moved on the norns itself
struct HwEvent
{
    uint8_t kind = 0;           // 0 key, 1 encoder
    uint8_t n = 0;              // 1..3
    int16_t v = 0;              // key: 1 down / 0 up; encoder: delta
};

// single producer, single consumer, fixed size, no allocation
template <typename T, size_t N>
class Spsc
{
public:
    bool push (const T& x) noexcept
    {
        const auto w = write.load (std::memory_order_relaxed);
        if (w - read.load (std::memory_order_acquire) >= N) return false;
        buf[w % N] = x;
        write.store (w + 1, std::memory_order_release);
        return true;
    }
    bool pop (T& x) noexcept
    {
        const auto r = read.load (std::memory_order_relaxed);
        if (r == write.load (std::memory_order_acquire)) return false;
        x = buf[r % N];
        read.store (r + 1, std::memory_order_release);
        return true;
    }
private:
    std::array<T, N> buf {};
    std::atomic<uint64_t> write { 0 }, read { 0 };
};

class NornsLink
{
public:
    NornsLink();
    ~NornsLink();

    // ---- configuration (message thread) --------------------------------------------------------
    void start();
    void stop();
    void setEnabled (bool);
    bool isEnabled() const noexcept { return enabled.load(); }
    void setHost (const std::string&);                    // "norns.local", an IP; port 10111 unless "host:port"
    std::string getHost() const;
    void setClientName (const std::string&);              // shown in the mod's menu (the track name)

    // ---- commands (message thread) ----------------------------------------------------------------
    void key (int n, int z);
    void enc (int n, int d);
    void setParam (const std::string& id, float raw01);
    void psetLoad (int n);
    void psetSave (int n);
    void requestDump();
    void setScreenFps (int fps);                          // 0: no mirror (editor closed)

    // ---- slots: host parameters bound to norns parameters --------------------------------------------
    void bindSlot (int slot, const std::string& paramId); // "" unbinds (message thread)
    std::string slotBinding (int slot) const;
    void setSlot (int slot, float v) noexcept;            // audio thread: the slot's value, CONDUIT included
    float slotSent (int slot) const noexcept { return slotOut[(size_t) slot].load (std::memory_order_relaxed); }

    // ---- audio thread -------------------------------------------------------------------------------
    void setClock (double bpm, bool playing, int mode) noexcept;   // mode 0 off, 1 tempo, 2 tempo + transport
    void pressKey (int n) noexcept;                                 // press and release (a family trigger)
    bool popHw (HwEvent& e) noexcept { return hwRing.pop (e); }

    // ---- state (message thread) ----------------------------------------------------------------------
    struct Snapshot
    {
        Status status = Status::Off;
        bool mod = false;
        int version = 0, epoch = 0, expected = 0, received = 0;
        float latencyMs = -1.0f;
        std::string script, address;
        std::vector<ParamInfo> params;
    };
    Snapshot snapshot (bool withParams = true) const;
    uint32_t paramsSerial() const noexcept { return pserial.load (std::memory_order_acquire); }   // bumps on any list / value change
    uint32_t listSerial() const noexcept { return lserial.load (std::memory_order_acquire); }     // bumps when the list itself changes
    Status status() const noexcept { return (Status) statusNow.load (std::memory_order_relaxed); }
    bool findParam (const std::string& id, ParamInfo& out) const;

    // the screen, one byte (0..15) per pixel, row-major; copies only when newer than `serial`
    bool readScreen (uint8_t* out, uint32_t& serial) const;
    uint32_t screenSerial() const noexcept { return sserial.load (std::memory_order_acquire); }

    // hardware activity counters for the panel (key 1..3 then encoder 1..3)
    uint32_t hwCount (int i) const noexcept { return hwCounts[(size_t) i].load (std::memory_order_relaxed); }

    // tests: run the loop by hand instead of the thread, with a fake clock
    void pump (double nowSeconds, int waitMs = 0);
    uint16_t localPort() const noexcept { return sock.port(); }

private:
    struct Resolve;
    void run();
    void openSocket();
    void handle (const osc::Message&, const sockaddr_in& from, double now);
    void send (const osc::Message&);
    void beginResolve (double now);
    void sendSlots (double now);
    void sendClock (double now);
    void sendKeys (double now);
    void heal (double now);
    void setStatus (Status s) noexcept { statusNow.store ((int) s, std::memory_order_relaxed); }
    static double clockNow();

    // thread
    std::thread th;
    std::atomic<bool> running { false }, enabled { true };
    Udp sock;

    // configuration
    mutable std::mutex cfgMutex;
    std::string host = "norns.local", clientName;
    uint16_t port = kNornsPort;
    std::atomic<uint32_t> hostSerial { 1 };
    uint32_t hostSeen = 0;
    std::shared_ptr<Resolve> resolving;
    Address norns;
    double resolvedAt = -1.0e9, nextResolve = 0.0;

    // commands queued from the message thread
    std::mutex cmdMutex;
    std::vector<osc::Message> cmds;
    std::atomic<int> screenFps { 0 };
    int screenSent = -1;
    double screenAt = -1.0e9;

    // slots
    mutable std::mutex slotMutex;
    std::array<std::string, kSlots> binding;
    std::atomic<uint32_t> bindSerial { 0 };
    uint32_t bindSeen = 0;
    std::array<std::string, kSlots> boundId;                // the link thread's copy
    std::array<std::atomic<float>, kSlots> slotIn {}, slotOut {};
    std::array<float, kSlots> slotLast {};
    std::array<double, kSlots> slotAt {};

    // clock
    std::atomic<double> bpm { 120.0 };
    std::atomic<bool> playing { false };
    std::atomic<int> clockMode { 0 };
    double clockBpmSent = -1.0, clockAt = -1.0e9;
    int clockPlaySent = -1;

    // keys pressed from the audio thread (family triggers)
    std::array<std::atomic<uint32_t>, 4> keyPresses {};
    std::array<uint32_t, 4> keyPressSeen {};
    std::array<double, 4> keyRelease {};

    // connection
    std::atomic<int> statusNow { (int) Status::Off };
    double helloAt = -1.0e9, pingAt = -1.0e9, heardAt = -1.0e9, waitingSince = 0.0;
    bool everHeard = false;
    uint32_t pingSeq = 0;
    std::array<double, 16> pingSent {};
    std::atomic<float> latency { -1.0f };
    std::atomic<bool> modSeen { false };
    std::atomic<int> modVersion { 0 };

    // the parameter list
    mutable std::mutex stateMutex;
    std::string script = "none";
    int epoch = -1, expected = 0;
    bool listOpen = false;
    std::vector<ParamInfo> params;
    double healAt = 0.0;
    std::atomic<uint32_t> pserial { 0 }, lserial { 0 };

    // the screen
    mutable std::mutex screenMutex;
    std::array<uint8_t, kScreenW * kScreenH> screen {};
    std::atomic<uint32_t> sserial { 0 };

    // hardware
    Spsc<HwEvent, 512> hwRing;
    std::array<std::atomic<uint32_t>, 6> hwCounts {};
};

} // namespace tether
