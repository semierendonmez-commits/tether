// TETHER engine tests, no JUCE: the OSC codec, then the link against the real norns mod.
// The mod (norns/tether/lib/mod.lua) runs in a stand-in norns (tests/norns_sim.lua, needs lua5.3 or
// lua5.4); this test carries its messages over real UDP on 127.0.0.1, so both sides of the
// protocol are the shipping code.
#include "NornsSim.h"
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

static int checks = 0, failures = 0;
#define CHECK(cond, ...) do { ++checks; if (! (cond)) { ++failures; std::printf ("FAIL %s:%d  %s  ", __FILE__, __LINE__, #cond); std::printf (__VA_ARGS__); std::printf ("\n"); } } while (0)

using namespace tether;

// ---- the codec ------------------------------------------------------------------------------------
static void codec()
{
    osc::Message m ("/tether/p");
    m.add (3).add ("cutoff").add (0.25f).add ("350 hz");
    const auto b = osc::encode (m);
    CHECK (b.size() % 4 == 0, "padded to 4");
    std::vector<osc::Message> out;
    osc::decode (b.data(), b.size(), out);
    CHECK (out.size() == 1, "one message");
    CHECK (out[0].path == "/tether/p" && out[0].i (0) == 3 && out[0].s (1) == "cutoff" && out[0].f (2) == 0.25f && out[0].s (3) == "350 hz", "round trip");
    CHECK (out[0].f (0) == 3.0f && out[0].i (2) == 0, "lenient numbers");
    CHECK (out[0].i (9, -7) == -7 && out[0].s (9, "x") == "x", "fallbacks");

    // a bundle of two, from another sender (doubles and T)
    std::vector<uint8_t> bundle = { '#', 'b', 'u', 'n', 'd', 'l', 'e', 0, 0, 0, 0, 0, 0, 0, 0, 1 };
    auto add = [&] (const std::vector<uint8_t>& msg)
    {
        const uint32_t n = (uint32_t) msg.size();
        for (int k = 3; k >= 0; --k) bundle.push_back ((uint8_t) (n >> (k * 8)));
        bundle.insert (bundle.end(), msg.begin(), msg.end());
    };
    osc::Message a ("/a");
    a.add (1);
    add (osc::encode (a));
    std::vector<uint8_t> d = { '/', 'd', 0, 0, ',', 'd', 'T', 0 };
    const double v = 2.5;
    uint64_t u;
    std::memcpy (&u, &v, 8);
    for (int k = 7; k >= 0; --k) d.push_back ((uint8_t) (u >> (k * 8)));
    add (d);
    out.clear();
    osc::decode (bundle.data(), bundle.size(), out);
    CHECK (out.size() == 2 && out[0].path == "/a" && out[1].f (0) == 2.5f && out[1].i (1) == 1, "bundle, double, T");

    // malformed packets are dropped, not read past their end
    out.clear();
    osc::decode (b.data(), 10, out);
    const std::vector<uint8_t> junk = { 'x', 'y', 0, 0 };
    osc::decode (junk.data(), junk.size(), out);
    CHECK (out.empty(), "junk dropped");

    // blobs
    osc::Message bl ("/b");
    osc::Arg x;
    x.type = 'b';
    x.s = std::string ("\0\1\2", 3);
    bl.args.push_back (x);
    const auto bb = osc::encode (bl);
    out.clear();
    osc::decode (bb.data(), bb.size(), out);
    CHECK (out.size() == 1 && out[0].s (0).size() == 3 && out[0].s (0)[2] == 2, "blob");
}

struct Rig
{
    Sim sim;
    Udp norns;               // the norns side of the wire
    NornsLink link;
    double t = 1000.0;
    int sentToNorns = 0;

    bool start (const std::string& lua)
    {
        if (! norns.open (0, true)) return false;
        if (! sim.start (lua, simScript())) return false;
        link.setHost ("127.0.0.1:" + std::to_string (norns.port()));
        link.setClientName ("Track 3");
        return true;
    }

    void deliver (const std::vector<std::pair<int, osc::Message>>& msgs)
    {
        Address to = Udp::resolve ("127.0.0.1", 0);
        for (const auto& [port, m] : msgs)
        {
            to.sa.sin_port = htons ((uint16_t) port);
            const auto b = osc::encode (m);
            norns.send (to, b.data(), b.size());
        }
    }

    // one round: the link runs, its packets reach the mod, the mod ticks, its answers come back
    void step (int ticks = 1)
    {
        t += 1.0 / 30.0;
        link.pump (t, 2);
        uint8_t buf[65536];
        sockaddr_in from {};
        int n;
        while ((n = norns.receive (buf, sizeof (buf), from, 2)) > 0)
        {
            std::vector<osc::Message> ms;
            osc::decode (buf, (size_t) n, ms);
            for (const auto& m : ms)
            {
                ++sentToNorns;
                deliver (sim.cmd ("IN\t" + std::to_string (ntohs (from.sin_port)) + "\t" + m.path + argLine (m)));
            }
        }
        deliver (sim.cmd ("TICK\t" + std::to_string (ticks)));
        link.pump (t, 2);
    }

    template <typename F> bool until (F&& f, int steps = 200)
    {
        for (int i = 0; i < steps; ++i) { if (f()) return true; step(); }
        return f();
    }
};

static void endToEnd (const std::string& lua)
{
    Rig r;
    if (! r.start (lua)) { CHECK (false, "could not start the stand-in norns with %s", lua.c_str()); return; }

    // ---- hello, welcome, the list
    CHECK (r.until ([&] { return r.link.status() == Status::Connected; }), "connects: %s", statusName (r.link.status()));
    CHECK (r.until ([&] { auto s = r.link.snapshot (false); return s.expected == 47 && s.received == 47; }), "the whole list arrives");
    auto s = r.link.snapshot();
    CHECK (s.script == "awake" && s.mod && s.version == 1, "script %s mod %d v%d", s.script.c_str(), (int) s.mod, s.version);
    CHECK (s.params.size() == 47 && s.params[0].id == "cutoff" && s.params[0].text == "350 hz", "first row");
    CHECK (s.params[1].steps == 16 && s.params[2].steps == 4 && s.params[2].text == "major", "stepped rows");
    CHECK (r.sim.logs.empty(), "no stray key / enc");
    CHECK (s.latencyMs >= 0.0f, "latency measured");

    // ---- the plug-in writes a parameter
    r.link.setParam ("scale", 1.0f);
    r.until ([&] { ParamInfo p; return r.link.findParam ("scale", p) && p.text == "lydian"; });
    ParamInfo p;
    CHECK (r.link.findParam ("scale", p) && p.text == "lydian" && p.raw == 1.0f, "option set and echoed: %s", p.text.c_str());

    // ---- a change made on norns comes back
    r.sim.cmd ("SET\tlvl3\t0.9");
    CHECK (r.until ([&] { ParamInfo q; return r.link.findParam ("lvl3", q) && std::abs (q.raw - 0.9f) < 1e-4f; }), "norns-side change arrives");

    // ---- slots: bound, moved by the audio thread
    r.link.setSlot (0, 0.25f);
    r.link.bindSlot (0, "cutoff");
    r.step (); r.step ();
    r.sim.vals.clear();
    r.sim.cmd ("GET\tcutoff");
    CHECK (r.sim.vals.size() == 1 && std::abs (std::atof (r.sim.vals[0].c_str()) - 0.25) < 1e-6, "binding alone does not move it");
    r.link.setSlot (0, 0.75f);
    r.step (); r.step ();
    r.sim.vals.clear();
    r.sim.cmd ("GET\tcutoff");
    CHECK (r.sim.vals.size() == 1 && std::abs (std::atof (r.sim.vals[0].c_str()) - 0.75) < 1e-5, "slot drives cutoff: %s", r.sim.vals.empty() ? "-" : r.sim.vals[0].c_str());
    CHECK (std::abs (r.link.slotSent (0) - 0.75f) < 1e-6f, "slot sent");
    r.link.bindSlot (0, "");
    r.link.setSlot (0, 0.1f);
    r.step (); r.step ();
    r.sim.vals.clear();
    r.sim.cmd ("GET\tcutoff");
    CHECK (std::abs (std::atof (r.sim.vals[0].c_str()) - 0.75) < 1e-5, "unbound slot sends nothing");

    // ---- keys and encoders from the panel, and from a family trigger
    r.sim.logs.clear();
    r.link.key (2, 1);
    r.link.key (2, 0);
    r.link.enc (3, -4);
    r.link.pressKey (3);
    for (int i = 0; i < 4; ++i) r.step();
    bool k2 = false, e3 = false, k3d = false, k3u = false;
    for (const auto& l : r.sim.logs)
    {
        k2 |= l == "key 2 1";
        e3 |= l == "enc 3 -4";
        k3d |= l == "key 3 1";
        k3u |= l == "key 3 0";
    }
    CHECK (k2 && e3 && k3d && k3u, "keys and encoders injected (%zu logs)", r.sim.logs.size());

    // ---- the hardware, back to the plug-in
    std::vector<HwEvent> hw;
    HwEvent e;
    r.deliver (r.sim.cmd ("HW\tenc\t2\t3"));
    r.deliver (r.sim.cmd ("HW\tkey\t1\t1"));
    for (int i = 0; i < 4 && hw.size() < 2; ++i) { r.step(); while (r.link.popHw (e)) hw.push_back (e); }
    bool sawEnc = false, sawKey = false;
    for (const auto& h : hw) { sawEnc |= h.kind == 1 && h.n == 2 && h.v == 3; sawKey |= h.kind == 0 && h.n == 1 && h.v == 1; }
    CHECK (sawEnc && sawKey, "hardware events arrive (%zu)", hw.size());
    CHECK (r.link.hwCount (3 + 1) >= 1 && r.link.hwCount (0) >= 1, "hardware counters");

    // ---- the screen mirror
    r.link.setScreenFps (30);
    r.deliver (r.sim.cmd ("PAINT\t2\t15"));
    uint8_t scr[kScreenW * kScreenH] {};
    uint32_t serial = 0;
    CHECK (r.until ([&] { r.link.readScreen (scr, serial); return scr[2 * 1024 + 5] == 15; }), "screen band arrives");
    CHECK (scr[0] == 0 && scr[3 * 1024] == 0 && scr[2 * 1024 + 1023] == 15, "band in place");
    r.link.setScreenFps (0);
    r.step (); r.step ();
    const auto ss = r.link.screenSerial();
    r.deliver (r.sim.cmd ("PAINT\t5\t7"));
    for (int i = 0; i < 10; ++i) r.step();
    CHECK (r.link.screenSerial() == ss, "screen stops when closed");

    // ---- tempo and transport
    r.link.setClock (172.0, true, 2);
    r.until ([&] { r.sim.vals.clear(); r.sim.cmd ("TEMPO"); return ! r.sim.vals.empty() && r.sim.vals[0] == "172.0 true"; }, 40);
    CHECK (! r.sim.vals.empty() && r.sim.vals[0] == "172.0 true", "tempo and start: %s", r.sim.vals.empty() ? "-" : r.sim.vals[0].c_str());

    // ---- pset
    r.sim.logs.clear();
    r.link.psetLoad (4);
    r.link.psetSave (5);
    r.step (); r.step ();
    bool rd = false, wr = false;
    for (const auto& l : r.sim.logs) { rd |= l == "pset_read 4"; wr |= l == "pset_write 5"; }
    CHECK (rd && wr, "pset load / save");

    // ---- another script: a new list, announced
    const auto ls = r.link.listSerial();
    r.deliver (r.sim.cmd ("SCRIPT\tmlr"));
    CHECK (r.until ([&] { auto q = r.link.snapshot (false); return q.script == "mlr" && q.expected == 46 && q.received == 46; }), "new script's list");
    CHECK (r.link.listSerial() != ls, "list serial moved");
    CHECK (! r.link.findParam ("cutoff", p), "old rows gone");

    // ---- lost rows heal: drop the dump, then let the link ask
    {
        r.link.requestDump();
        r.step ();                                           // the dump starts; throw away what comes back
        uint8_t buf[65536]; sockaddr_in from {};
        while (r.norns.receive (buf, sizeof (buf), from, 2) > 0) {}
        auto dropped = r.sim.cmd ("TICK\t1");                // rows 1-24 never arrive
        (void) dropped;
        CHECK (r.until ([&] { auto q = r.link.snapshot (false); return q.received == q.expected && q.expected == 46; }), "holes healed");
    }

    // ---- the norns goes quiet: LOST, then back
    for (int i = 0; i < 160; ++i) { r.t += 1.0 / 30.0; r.link.pump (r.t, 0); uint8_t b[65536]; sockaddr_in f {}; while (r.norns.receive (b, sizeof (b), f, 0) > 0) {} }
    CHECK (r.link.status() == Status::Lost || r.link.status() == Status::Resolving || r.link.status() == Status::Waiting, "quiet norns: %s", statusName (r.link.status()));
    CHECK (r.until ([&] { return r.link.status() == Status::Connected; }), "reconnects: %s", statusName (r.link.status()));

    // ---- disabled: nothing goes out
    r.link.setEnabled (false);
    r.step (); r.step ();                                    // the bye, and what was already on the way
    const int before = r.sentToNorns;
    for (int i = 0; i < 20; ++i) r.step();
    CHECK (r.sentToNorns == before && r.link.status() == Status::Off, "off is silent");

    r.sim.stop();
}

static void noNorns()
{
    // an address nobody answers on: WAITING, then NO REPLY; keys still go out through /remote
    Udp silent;
    silent.open (0, true);
    NornsLink link;
    link.setHost ("127.0.0.1:" + std::to_string (silent.port()));
    double t = 0.0;
    for (int i = 0; i < 20 && link.status() != Status::Waiting; ++i) { t += 0.05; link.pump (t, 5); }
    CHECK (link.status() == Status::Waiting, "waiting: %s", statusName (link.status()));
    link.key (2, 1);
    for (int i = 0; i < 100; ++i) { t += 0.05; link.pump (t, 0); }
    CHECK (link.status() == Status::NoMod, "no reply: %s", statusName (link.status()));
    uint8_t buf[2048]; sockaddr_in from {};
    bool remote = false, hello = false;
    int n;
    while ((n = silent.receive (buf, sizeof (buf), from, 0)) > 0)
    {
        std::vector<osc::Message> ms;
        osc::decode (buf, (size_t) n, ms);
        for (const auto& m : ms) { remote |= m.path == "/remote/key" && m.i (0) == 2; hello |= m.path == "/tether/hello"; }
    }
    CHECK (remote && hello, "remote key %d hello %d", (int) remote, (int) hello);

    NornsLink bad;
    bad.setHost ("no-such-host.invalid");
    for (int i = 0; i < 400 && bad.status() != Status::NotFound; ++i) { bad.pump (i * 0.01, 10); }
    CHECK (bad.status() == Status::NotFound, "unknown host: %s", statusName (bad.status()));
}

static void threaded()
{
    // the real thread: start and stop quickly, twice (the plug-in is opened and closed in a host)
    for (int k = 0; k < 2; ++k)
    {
        NornsLink link;
        link.setHost ("no-such-host.invalid");
        link.start();
        std::this_thread::sleep_for (std::chrono::milliseconds (30));
        link.stop();
    }
    CHECK (true, "start / stop");
}

int main()
{
    codec();
    const std::string lua = findLua();
    if (lua.empty()) std::printf ("(no lua: end-to-end test skipped)\n");
    else endToEnd (lua);
    noNorns();
    threaded();
    std::printf ("engine: %d checks, %d failed\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
