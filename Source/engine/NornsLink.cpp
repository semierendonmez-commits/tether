#include "NornsLink.h"
#include <algorithm>
#include <cstdlib>

namespace tether
{

const char* statusName (Status s) noexcept
{
    switch (s)
    {
        case Status::Off:       return "OFF";
        case Status::Resolving: return "LOOKING UP";
        case Status::NotFound:  return "NOT FOUND";
        case Status::Waiting:   return "WAITING";
        case Status::NoMod:     return "NO REPLY";
        case Status::Connected: return "CONNECTED";
        case Status::Lost:      return "LOST";
    }
    return "";
}

// getaddrinfo on a .local name can block for seconds: it runs on a detached helper so that
// stopping the link (closing the plug-in) never waits for it
struct NornsLink::Resolve
{
    std::atomic<bool> done { false };
    Address result;
    uint32_t serial = 0;
    static std::shared_ptr<Resolve> launch (const std::string& host, uint16_t port, uint32_t serial)
    {
        auto r = std::make_shared<Resolve>();
        r->serial = serial;
        std::thread ([r, host, port] { r->result = Udp::resolve (host, port); r->done.store (true, std::memory_order_release); }).detach();
        return r;
    }
};

namespace
{
    constexpr double kHelloEvery = 1.0, kPingEvery = 1.0, kQuietLost = 4.0, kNoModAfter = 3.0, kReresolveLost = 6.0;
    constexpr double kSlotMinGap = 0.008;       // ~120 Hz per slot at most
    constexpr float kSlotEps = 1.0e-4f;

    void splitHost (const std::string& in, std::string& host, uint16_t& port)
    {
        host = in;
        port = kNornsPort;
        const auto c = in.rfind (':');
        if (c != std::string::npos && c + 1 < in.size())
        {
            const int p = std::atoi (in.c_str() + c + 1);
            if (p > 0 && p < 65536) { host = in.substr (0, c); port = (uint16_t) p; }
        }
        while (! host.empty() && host.back() == ' ') host.pop_back();
        while (! host.empty() && host.front() == ' ') host.erase (host.begin());
    }

    int hexv (char c)
    {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        return 0;
    }
}

double NornsLink::clockNow()
{
    using namespace std::chrono;
    return duration<double> (steady_clock::now().time_since_epoch()).count();
}

NornsLink::NornsLink()
{
    for (auto& v : slotIn) v.store (0.0f);
    for (auto& v : slotOut) v.store (0.0f);
    slotLast.fill (std::nanf (""));
    slotAt.fill (-1.0e9);
    keyRelease.fill (-1.0);
}

NornsLink::~NornsLink() { stop(); }

void NornsLink::openSocket()
{
    if (! sock.isOpen()) sock.open (0);
}

void NornsLink::start()
{
    if (running.exchange (true)) return;
    openSocket();
    th = std::thread ([this] { run(); });
}

void NornsLink::stop()
{
    if (! running.exchange (false)) return;
    if (th.joinable()) th.join();
    if (sock.isOpen() && norns.valid)
    {
        osc::Message bye ("/tether/bye");
        bye.add ((int) sock.port());
        send (bye);
    }
    sock.close();
}

void NornsLink::run()
{
    while (running.load())
        pump (clockNow(), 5);
}

void NornsLink::setEnabled (bool on)
{
    if (enabled.exchange (on) == on) return;
    hostSerial.fetch_add (1);                              // a fresh start: resolve again, say hello
}

void NornsLink::setHost (const std::string& h)
{
    {
        std::lock_guard<std::mutex> l (cfgMutex);
        std::string hh;
        uint16_t pp;
        splitHost (h, hh, pp);
        if (hh == host && pp == port) return;
        host = hh;
        port = pp;
    }
    hostSerial.fetch_add (1);
}

std::string NornsLink::getHost() const
{
    std::lock_guard<std::mutex> l (cfgMutex);
    return port == kNornsPort ? host : host + ":" + std::to_string (port);
}

void NornsLink::setClientName (const std::string& n)
{
    std::lock_guard<std::mutex> l (cfgMutex);
    clientName = n;
}

// ---- commands ------------------------------------------------------------------------------------
void NornsLink::key (int n, int z)
{
    if (n < 1 || n > 3) return;
    osc::Message m (modSeen.load() ? "/tether/key" : "/remote/key");   // core norns understands /remote too
    m.add (n).add (z);
    std::lock_guard<std::mutex> l (cmdMutex);
    cmds.push_back (std::move (m));
}

void NornsLink::enc (int n, int d)
{
    if (n < 1 || n > 3 || d == 0) return;
    osc::Message m (modSeen.load() ? "/tether/enc" : "/remote/enc");
    m.add (n).add (d);
    std::lock_guard<std::mutex> l (cmdMutex);
    cmds.push_back (std::move (m));
}

void NornsLink::setParam (const std::string& id, float raw01)
{
    if (id.empty()) return;
    osc::Message m ("/tether/set");
    m.add (id).add (std::clamp (raw01, 0.0f, 1.0f));
    {
        std::lock_guard<std::mutex> l (stateMutex);          // show it at once; the mod's echo confirms it
        for (auto& p : params)
            if (p.known && p.id == id && p.type != TTrigger) { p.raw = std::clamp (raw01, 0.0f, 1.0f); break; }
    }
    pserial.fetch_add (1, std::memory_order_release);
    std::lock_guard<std::mutex> l (cmdMutex);
    cmds.push_back (std::move (m));
}

void NornsLink::psetLoad (int n)
{
    if (n < 1) return;
    osc::Message m ("/tether/pset/load");
    m.add (n);
    std::lock_guard<std::mutex> l (cmdMutex);
    cmds.push_back (std::move (m));
}

void NornsLink::psetSave (int n)
{
    if (n < 1) return;
    osc::Message m ("/tether/pset/save");
    m.add (n).add ("tether " + std::to_string (n));
    std::lock_guard<std::mutex> l (cmdMutex);
    cmds.push_back (std::move (m));
}

void NornsLink::requestDump()
{
    osc::Message m ("/tether/dump");
    m.add (0);                                              // the port is filled in when sent
    std::lock_guard<std::mutex> l (cmdMutex);
    cmds.push_back (std::move (m));
}

void NornsLink::setScreenFps (int fps) { screenFps.store (std::clamp (fps, 0, 30)); }

void NornsLink::requestAudioDaemon()
{
    osc::Message m ("/tether/audio/start");
    std::lock_guard<std::mutex> l (cmdMutex);
    cmds.push_back (std::move (m));
}

std::string NornsLink::ipAddress() const
{
    std::lock_guard<std::mutex> l (stateMutex);
    return addrIp;
}

// ---- slots ----------------------------------------------------------------------------------------
void NornsLink::bindSlot (int slot, const std::string& paramId)
{
    if (slot < 0 || slot >= kSlots) return;
    std::lock_guard<std::mutex> l (slotMutex);
    if (binding[(size_t) slot] == paramId) return;
    binding[(size_t) slot] = paramId;
    bindSerial.fetch_add (1, std::memory_order_release);
}

std::string NornsLink::slotBinding (int slot) const
{
    if (slot < 0 || slot >= kSlots) return {};
    std::lock_guard<std::mutex> l (slotMutex);
    return binding[(size_t) slot];
}

void NornsLink::setSlot (int slot, float v) noexcept
{
    if (slot >= 0 && slot < kSlots) slotIn[(size_t) slot].store (std::clamp (v, 0.0f, 1.0f), std::memory_order_relaxed);
}

void NornsLink::setClock (double b, bool p, int mode) noexcept
{
    if (b > 0.0) bpm.store (b, std::memory_order_relaxed);
    playing.store (p, std::memory_order_relaxed);
    clockMode.store (mode, std::memory_order_relaxed);
}

void NornsLink::pressKey (int n) noexcept
{
    if (n >= 1 && n <= 3) keyPresses[(size_t) n].fetch_add (1, std::memory_order_relaxed);
}

// ---- state ------------------------------------------------------------------------------------------
NornsLink::Snapshot NornsLink::snapshot (bool withParams) const
{
    Snapshot s;
    s.status = status();
    s.mod = modSeen.load();
    s.version = modVersion.load();
    s.latencyMs = latency.load();
    {
        std::lock_guard<std::mutex> l (stateMutex);
        s.script = script;
        s.epoch = epoch;
        s.expected = expected;
        for (const auto& p : params) s.received += p.known ? 1 : 0;
        if (withParams) s.params = params;
    }
    {
        std::lock_guard<std::mutex> l (stateMutex);
        s.address = addrText;
    }
    return s;
}

bool NornsLink::findParam (const std::string& id, ParamInfo& out) const
{
    std::lock_guard<std::mutex> l (stateMutex);
    for (const auto& p : params)
        if (p.known && p.id == id) { out = p; return true; }
    return false;
}

bool NornsLink::readScreen (uint8_t* out, uint32_t& serial) const
{
    const auto now = sserial.load (std::memory_order_acquire);
    if (now == serial) return false;
    std::lock_guard<std::mutex> l (screenMutex);
    std::copy (screen.begin(), screen.end(), out);
    serial = sserial.load (std::memory_order_acquire);
    return true;
}

// ---- the loop -----------------------------------------------------------------------------------------
void NornsLink::send (const osc::Message& m)
{
    if (! norns.valid) return;
    const auto b = osc::encode (m);
    sock.send (norns, b.data(), b.size());
}

void NornsLink::beginResolve (double now)
{
    std::string h;
    uint16_t p;
    {
        std::lock_guard<std::mutex> l (cfgMutex);
        h = host;
        p = port;
    }
    norns = Address {};
    if (h.empty()) { setStatus (Status::NotFound); nextResolve = now + 1.0e9; return; }
    resolving = Resolve::launch (h, p, hostSeen);
    setStatus (Status::Resolving);
}

void NornsLink::pump (double now, int waitMs)
{
    openSocket();

    // a new host, or switched on / off: start over
    const uint32_t hs = hostSerial.load();
    if (hs != hostSeen)
    {
        hostSeen = hs;
        resolving.reset();
        if (norns.valid && everHeard)                       // tell the old one to stop streaming to us
        {
            osc::Message bye ("/tether/bye");
            bye.add ((int) sock.port());
            send (bye);
        }
        norns = Address {};
        everHeard = false;
        heardAt = helloAt = pingAt = -1.0e9;
        screenSent = -1;
        clockBpmSent = -1.0;
        clockPlaySent = -1;
        modSeen.store (false);
        latency.store (-1.0f);
        {
            std::lock_guard<std::mutex> l (stateMutex);
            params.clear();
            script = "none";
            epoch = -1;
            expected = 0;
            listOpen = false;
        }
        lserial.fetch_add (1);
        pserial.fetch_add (1);
        nextResolve = now;
        setStatus (enabled.load() ? Status::Resolving : Status::Off);
    }

    if (! enabled.load())
    {
        setStatus (Status::Off);
        if (waitMs > 0) std::this_thread::sleep_for (std::chrono::milliseconds (waitMs));
        return;
    }

    // the address
    if (! norns.valid)
    {
        if (resolving == nullptr && now >= nextResolve) beginResolve (now);
        if (resolving != nullptr && resolving->done.load (std::memory_order_acquire))
        {
            if (resolving->serial == hostSeen && resolving->result.valid)
            {
                norns = resolving->result;
                resolvedAt = waitingSince = now;
                setStatus (Status::Waiting);
            }
            else
            {
                setStatus (Status::NotFound);
                nextResolve = now + 3.0;
            }
            resolving.reset();
        }
    }

    // everything that came in
    {
        uint8_t buf[65536];
        sockaddr_in from {};
        int wait = waitMs;
        for (int k = 0; k < 4096; ++k)
        {
            const int n = sock.receive (buf, sizeof (buf), from, wait);
            if (n <= 0) break;
            wait = 0;
            if (! norns.sameHost (from)) continue;          // only the norns we talk to
            std::vector<osc::Message> ms;
            osc::decode (buf, (size_t) n, ms);
            for (const auto& m : ms) handle (m, from, now);
        }
    }

    {
        std::lock_guard<std::mutex> l (stateMutex);
        addrIp = norns.valid ? norns.ip() : std::string();
        addrText = norns.valid ? norns.text() : std::string();
    }
    if (! norns.valid) return;

    // hello until welcomed, then a ping a second
    const bool quiet = now - heardAt > kQuietLost;
    if ((! everHeard || quiet) && now - helloAt >= kHelloEvery)
    {
        helloAt = now;
        std::string nm;
        {
            std::lock_guard<std::mutex> l (cfgMutex);
            nm = clientName;
        }
        osc::Message h ("/tether/hello");
        h.add ((int) sock.port()).add (nm);
        send (h);
    }
    if (now - pingAt >= kPingEvery)
    {
        pingAt = now;
        ++pingSeq;
        pingSent[pingSeq % pingSent.size()] = now;
        osc::Message p ("/tether/ping");
        p.add ((int) pingSeq).add ((int) sock.port());
        send (p);
    }

    if (everHeard) setStatus (quiet ? Status::Lost : Status::Connected);
    else setStatus (now - waitingSince > kNoModAfter ? Status::NoMod : Status::Waiting);

    // a norns that went away may have a new address (DHCP, another network): look it up again
    if (quiet && now - std::max (heardAt, resolvedAt) > kReresolveLost)
    {
        norns = Address {};
        everHeard = false;
        nextResolve = now;
        return;
    }

    // queued commands
    std::vector<osc::Message> out;
    {
        std::lock_guard<std::mutex> l (cmdMutex);
        out.swap (cmds);
    }
    for (auto& m : out)
    {
        if (m.path == "/tether/dump" && ! m.args.empty()) m.args[0].i = (int) sock.port();
        // queued before the mod was seen: the core understands /remote
        if (m.path == "/remote/key" && modSeen.load()) m.path = "/tether/key";
        if (m.path == "/remote/enc" && modSeen.load()) m.path = "/tether/enc";
        send (m);
    }

    sendKeys (now);
    if (modSeen.load())
    {
        sendSlots (now);
        sendClock (now);
        heal (now);

        const int fps = screenFps.load();
        if (fps != screenSent || (fps > 0 && now - screenAt > 2.0))
        {
            screenSent = fps;
            screenAt = now;
            osc::Message s ("/tether/screen");
            s.add (fps).add ((int) sock.port());
            send (s);
        }
    }
}

void NornsLink::handle (const osc::Message& m, const sockaddr_in&, double now)
{
    heardAt = now;
    const auto& p = m.path;
    if (p.rfind ("/tether/", 0) != 0) return;

    if (p == "/tether/welcome")
    {
        everHeard = true;
        modSeen.store (true);
        modVersion.store (m.i (0));
        screenSent = -1;                                    // the mod is new to us: tell it again
        clockBpmSent = -1.0;
        clockPlaySent = -1;
        slotLast.fill (std::nanf (""));
        return;
    }
    if (! everHeard)
    {
        // a mod that knew us from before (we restarted, it did not): carry on, ask for the list
        everHeard = true;
        modSeen.store (true);
        if (p != "/tether/params/begin") requestDump();
    }

    if (p == "/tether/pong")
    {
        const uint32_t seq = (uint32_t) m.i (0);
        if (seq != 0 && pingSeq - seq < pingSent.size())
            latency.store ((float) ((now - pingSent[seq % pingSent.size()]) * 1000.0));
        bool stale;
        {
            std::lock_guard<std::mutex> l (stateMutex);
            stale = m.size() > 1 && m.i (1) != epoch && ! listOpen;
        }
        if (stale) requestDump();                           // the mod restarted or a dump got lost
        return;
    }
    if (p == "/tether/params/begin")
    {
        {
            std::lock_guard<std::mutex> l (stateMutex);
            expected = std::clamp (m.i (0), 0, 4096);
            script = m.s (1, "none");
            epoch = m.i (2);
            params.assign ((size_t) expected, ParamInfo {});
            for (int i = 0; i < expected; ++i) params[(size_t) i].index = i + 1;
            listOpen = true;
        }
        healAt = now + 1.0;
        lserial.fetch_add (1, std::memory_order_release);
        pserial.fetch_add (1, std::memory_order_release);
        return;
    }
    if (p == "/tether/p")
    {
        const int i = m.i (0);
        {
            std::lock_guard<std::mutex> l (stateMutex);
            if (i < 1 || i > (int) params.size()) return;
            auto& x = params[(size_t) i - 1];
            const bool wasKnown = x.known;
            x.index = i;
            x.id = m.s (1);
            x.name = m.s (2, x.id);
            x.type = m.i (3);
            x.raw = std::clamp (m.f (4), 0.0f, 1.0f);
            x.text = m.s (5);
            x.steps = m.i (6);
            x.hidden = m.i (7) != 0;
            x.known = true;
            if (! wasKnown) lserial.fetch_add (1, std::memory_order_release);
        }
        pserial.fetch_add (1, std::memory_order_release);
        return;
    }
    if (p == "/tether/params/end")
    {
        {
            std::lock_guard<std::mutex> l (stateMutex);
            listOpen = false;
        }
        healAt = now;                                       // look for holes now
        return;
    }
    if (p == "/tether/v")
    {
        const int i = m.i (0);
        {
            std::lock_guard<std::mutex> l (stateMutex);
            if (i < 1 || i > (int) params.size() || ! params[(size_t) i - 1].known) return;
            auto& x = params[(size_t) i - 1];
            x.raw = std::clamp (m.f (1), 0.0f, 1.0f);
            x.text = m.s (2);
        }
        pserial.fetch_add (1, std::memory_order_release);
        return;
    }
    if (p == "/tether/script")
    {
        std::lock_guard<std::mutex> l (stateMutex);
        script = m.s (0, "none");                            // the new list follows (params/begin)
        return;
    }
    if (p == "/tether/scr")
    {
        const int b = m.i (0);
        const auto hex = m.s (1);
        if (b < 0 || b >= kBands || hex.size() < (size_t) (kScreenW * kBandRows)) return;
        {
            std::lock_guard<std::mutex> l (screenMutex);
            auto* dst = screen.data() + (size_t) b * kScreenW * kBandRows;
            for (int k = 0; k < kScreenW * kBandRows; ++k) dst[k] = (uint8_t) hexv (hex[(size_t) k]);
        }
        sserial.fetch_add (1, std::memory_order_release);
        return;
    }
    if (p == "/tether/hw")
    {
        HwEvent e;
        const auto kind = m.s (0);
        e.kind = kind == "enc" ? 1 : 0;
        e.n = (uint8_t) std::clamp (m.i (1), 1, 3);
        e.v = (int16_t) std::clamp (m.i (2), -32767, 32767);
        hwRing.push (e);
        hwCounts[(size_t) (e.kind * 3 + e.n - 1)].fetch_add (1, std::memory_order_relaxed);
        return;
    }
}

void NornsLink::sendSlots (double now)
{
    const uint32_t bs = bindSerial.load (std::memory_order_acquire);
    if (bs != bindSeen)
    {
        bindSeen = bs;
        std::lock_guard<std::mutex> l (slotMutex);
        for (int s = 0; s < kSlots; ++s)
        {
            if (boundId[(size_t) s] != binding[(size_t) s])
            {
                boundId[(size_t) s] = binding[(size_t) s];
                // a new binding starts from where the slot is now: no jump until it moves
                slotLast[(size_t) s] = slotIn[(size_t) s].load (std::memory_order_relaxed);
            }
        }
    }
    for (int s = 0; s < kSlots; ++s)
    {
        if (boundId[(size_t) s].empty()) continue;
        const float v = slotIn[(size_t) s].load (std::memory_order_relaxed);
        const float last = slotLast[(size_t) s];
        if (! std::isnan (last) && std::abs (v - last) < kSlotEps) continue;
        if (now - slotAt[(size_t) s] < kSlotMinGap) continue;
        slotLast[(size_t) s] = v;
        slotAt[(size_t) s] = now;
        slotOut[(size_t) s].store (v, std::memory_order_relaxed);
        osc::Message m ("/tether/set");
        m.add (boundId[(size_t) s]).add (v);
        send (m);
    }
}

void NornsLink::sendClock (double now)
{
    const int mode = clockMode.load (std::memory_order_relaxed);
    if (mode <= 0) { clockBpmSent = -1.0; clockPlaySent = -1; return; }
    const double b = bpm.load (std::memory_order_relaxed);
    const int pl = playing.load (std::memory_order_relaxed) ? 1 : 0;
    const bool changed = std::abs (b - clockBpmSent) > 0.001 || pl != clockPlaySent;
    if ((changed && now - clockAt >= 0.1) || now - clockAt >= 2.0)
    {
        clockAt = now;
        clockBpmSent = b;
        clockPlaySent = pl;
        osc::Message m ("/tether/clock");
        m.add ((float) b).add (pl).add (mode);
        send (m);
    }
}

void NornsLink::sendKeys (double now)
{
    const bool mod = modSeen.load();
    for (int n = 1; n <= 3; ++n)
    {
        if (keyRelease[(size_t) n] > 0.0 && now >= keyRelease[(size_t) n])
        {
            keyRelease[(size_t) n] = -1.0;
            osc::Message m (mod ? "/tether/key" : "/remote/key");
            m.add (n).add (0);
            send (m);
        }
        const uint32_t c = keyPresses[(size_t) n].load (std::memory_order_relaxed);
        if (c != keyPressSeen[(size_t) n] && keyRelease[(size_t) n] < 0.0)
        {
            keyPressSeen[(size_t) n] = c;
            keyRelease[(size_t) n] = now + 0.04;
            osc::Message m (mod ? "/tether/key" : "/remote/key");
            m.add (n).add (1);
            send (m);
        }
    }
}

// rows lost on the way: ask for them again, a few runs at a time
void NornsLink::heal (double now)
{
    if (now < healAt) return;
    std::vector<std::pair<int, int>> runs;
    {
        std::lock_guard<std::mutex> l (stateMutex);
        if (listOpen && now < healAt + 1.0) return;         // still arriving
        int a = -1;
        for (int i = 0; i <= (int) params.size(); ++i)
        {
            const bool missing = i < (int) params.size() && ! params[(size_t) i].known;
            if (missing && a < 0) a = i + 1;
            if (! missing && a > 0) { runs.emplace_back (a, i); a = -1; if (runs.size() >= 8) break; }
        }
    }
    healAt = now + 0.5;
    for (const auto& r : runs)
    {
        osc::Message m ("/tether/range");
        m.add (r.first).add (r.second).add ((int) sock.port());
        send (m);
    }
}

} // namespace tether
