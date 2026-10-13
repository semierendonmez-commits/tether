// The stand-in norns shared by the tests and the snapshot tool: tests/norns_sim.lua (the real
// tether mod on a stub norns) in a child process, its messages carried over real UDP on 127.0.0.1.
// POSIX only (fork, pipes): the tests and tools run on macOS and Linux.
#pragma once

#include "engine/NornsLink.h"
#include <cstdio>
#include <cstdlib>
#include <mutex>
#include <string>
#include <thread>
#include <vector>
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>

namespace tether
{

struct Sim
{
    pid_t pid = -1;
    FILE* in = nullptr;      // to the sim
    FILE* out = nullptr;     // from the sim
    std::vector<std::string> logs, vals;

    bool start (const std::string& lua, const std::string& script)
    {
        int a[2], b[2];
        if (pipe (a) != 0 || pipe (b) != 0) return false;
        pid = fork();
        if (pid == 0)
        {
            dup2 (a[0], 0);
            dup2 (b[1], 1);
            ::close (a[1]);
            ::close (b[0]);
            execlp (lua.c_str(), lua.c_str(), script.c_str(), (char*) nullptr);
            _exit (127);
        }
        ::close (a[0]);
        ::close (b[1]);
        in = fdopen (a[1], "w");
        out = fdopen (b[0], "r");
        char line[256];
        return fgets (line, sizeof (line), out) != nullptr && std::string (line).rfind ("READY", 0) == 0;
    }

    void stop()
    {
        if (in) fclose (in);
        if (out) fclose (out);
        in = out = nullptr;
        if (pid > 0) { kill (pid, SIGTERM); waitpid (pid, nullptr, 0); pid = -1; }
    }

    // one command; returns the OSC it wants sent (port, message)
    std::vector<std::pair<int, osc::Message>> cmd (const std::string& c)
    {
        std::vector<std::pair<int, osc::Message>> msgs;
        if (in == nullptr) return msgs;
        std::fprintf (in, "%s\n", c.c_str());
        std::fflush (in);
        static thread_local std::vector<char> line (1 << 16);
        while (fgets (line.data(), (int) line.size(), out) != nullptr)
        {
            std::string s (line.data());
            while (! s.empty() && (s.back() == '\n' || s.back() == '\r')) s.pop_back();
            if (s == "END") break;
            std::vector<std::string> f;
            size_t p = 0;
            while (p <= s.size())
            {
                const auto t = s.find ('\t', p);
                f.push_back (s.substr (p, t == std::string::npos ? std::string::npos : t - p));
                if (t == std::string::npos) break;
                p = t + 1;
            }
            if (f[0] == "LOG") { std::string l; for (size_t k = 1; k < f.size(); ++k) l += (k > 1 ? " " : "") + f[k]; logs.push_back (l); continue; }
            if (f[0] == "VAL") { vals.push_back (f.size() > 1 ? f[1] + (f.size() > 2 ? " " + f[2] : "") : ""); continue; }
            if (f[0] != "OUT" || f.size() < 3) continue;
            osc::Message m (f[2]);
            for (size_t k = 3; k < f.size(); ++k)
            {
                const auto& t = f[k];
                if (t.rfind ("i:", 0) == 0) m.add (std::atoi (t.c_str() + 2));
                else if (t.rfind ("f:", 0) == 0) m.add ((float) std::atof (t.c_str() + 2));
                else m.add (t.size() > 2 ? t.substr (2) : std::string());
            }
            msgs.emplace_back (std::atoi (f[1].c_str()), std::move (m));
        }
        return msgs;
    }
};

inline std::string argLine (const osc::Message& m)
{
    std::string s;
    for (const auto& a : m.args)
    {
        s += '\t';
        if (a.type == 'f') { char b[32]; std::snprintf (b, sizeof (b), "f:%.9g", a.f); s += b; }
        else if (a.type == 's') s += "s:" + a.s;
        else s += "i:" + std::to_string (a.i);
    }
    return s;
}

inline std::string findLua()
{
    for (const char* c : { "lua5.4", "lua5.3", "lua" })
        if (std::system ((std::string ("command -v ") + c + " >/dev/null 2>&1").c_str()) == 0) return c;
    return {};
}

inline std::string simScript()
{
    const char* dir = std::getenv ("TETHER_TESTS_DIR");
    return std::string (dir != nullptr ? dir : "tests") + "/norns_sim.lua";
}

// the stand-in on its own thread: forwards everything, ticks the mod at 30 Hz
struct Bridge
{
    Sim sim;
    Udp norns;
    std::thread th;
    std::atomic<bool> running { false };
    std::mutex m;

    bool start()
    {
        const auto lua = findLua();
        if (lua.empty() || ! norns.open (0, true) || ! sim.start (lua, simScript())) return false;
        running = true;
        th = std::thread ([this] { loop(); });
        return true;
    }

    void stop()
    {
        if (! running.exchange (false)) return;
        th.join();
        sim.stop();
    }
    ~Bridge() { stop(); }

    std::string address() const { return "127.0.0.1:" + std::to_string (norns.port()); }

    void deliver (const std::vector<std::pair<int, osc::Message>>& msgs)
    {
        Address to = Udp::resolve ("127.0.0.1", 0);
        for (const auto& [port, msg] : msgs)
        {
            to.sa.sin_port = htons ((uint16_t) port);
            const auto b = osc::encode (msg);
            norns.send (to, b.data(), b.size());
        }
    }

    // a command to the stand-in (SET, HW, PAINT, GET ...); its OSC goes out at once
    std::vector<std::string> cmd (const std::string& c)
    {
        std::lock_guard<std::mutex> l (m);
        sim.vals.clear();
        deliver (sim.cmd (c));
        return sim.vals;
    }
    std::vector<std::string> logs()
    {
        std::lock_guard<std::mutex> l (m);
        return sim.logs;
    }

private:
    void loop()
    {
        using clk = std::chrono::steady_clock;
        auto next = clk::now();
        uint8_t buf[65536];
        while (running.load())
        {
            sockaddr_in from {};
            const int n = norns.receive (buf, sizeof (buf), from, 3);
            if (n > 0)
            {
                std::vector<osc::Message> ms;
                osc::decode (buf, (size_t) n, ms);
                std::lock_guard<std::mutex> l (m);
                for (const auto& msg : ms)
                    deliver (sim.cmd ("IN\t" + std::to_string (ntohs (from.sin_port)) + "\t" + msg.path + argLine (msg)));
            }
            if (clk::now() >= next)
            {
                next += std::chrono::milliseconds (33);
                std::lock_guard<std::mutex> l (m);
                deliver (sim.cmd ("TICK\t1"));
            }
        }
    }
};

} // namespace tether
