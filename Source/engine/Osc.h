// OSC 1.0 messages, the subset norns speaks: int32, float32, string, blob in; plus double, int64,
// true / false / nil read leniently (some senders use them). Bundles are unpacked. No JUCE.
#pragma once

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

namespace tether::osc
{

struct Arg
{
    char type = 'i';            // 'i' 'f' 's' 'b' (anything else is read into i / f)
    int32_t i = 0;
    float f = 0.0f;
    std::string s;              // string or blob bytes
};

struct Message
{
    std::string path;
    std::vector<Arg> args;

    Message() = default;
    explicit Message (std::string p) : path (std::move (p)) {}

    Message& add (int v)                { Arg a; a.type = 'i'; a.i = v; a.f = (float) v; args.push_back (a); return *this; }
    Message& add (float v)              { Arg a; a.type = 'f'; a.f = v; a.i = (int32_t) v; args.push_back (a); return *this; }
    Message& add (double v)             { return add ((float) v); }
    Message& add (const std::string& v) { Arg a; a.type = 's'; a.s = v; args.push_back (a); return *this; }
    Message& add (const char* v)        { return add (std::string (v != nullptr ? v : "")); }

    size_t size() const noexcept { return args.size(); }
    // lenient readers: numbers convert either way, a missing argument is the fallback
    int i (size_t n, int fallback = 0) const noexcept
    {
        if (n >= args.size()) return fallback;
        const auto& a = args[n];
        if (a.type == 'i') return a.i;
        if (a.type == 'f') return (int) a.f;
        if (a.type == 's') return std::atoi (a.s.c_str());
        return fallback;
    }
    float f (size_t n, float fallback = 0.0f) const noexcept
    {
        if (n >= args.size()) return fallback;
        const auto& a = args[n];
        if (a.type == 'f') return a.f;
        if (a.type == 'i') return (float) a.i;
        if (a.type == 's') return (float) std::atof (a.s.c_str());
        return fallback;
    }
    std::string s (size_t n, const std::string& fallback = {}) const
    {
        if (n >= args.size()) return fallback;
        const auto& a = args[n];
        if (a.type == 's' || a.type == 'b') return a.s;
        if (a.type == 'i') return std::to_string (a.i);
        if (a.type == 'f') return std::to_string (a.f);
        return fallback;
    }
};

namespace detail
{
    inline void pad (std::vector<uint8_t>& b) { while (b.size() % 4 != 0) b.push_back (0); }
    inline void str (std::vector<uint8_t>& b, const std::string& s) { b.insert (b.end(), s.begin(), s.end()); b.push_back (0); pad (b); }
    inline void be32 (std::vector<uint8_t>& b, uint32_t v) { for (int k = 3; k >= 0; --k) b.push_back ((uint8_t) (v >> (k * 8))); }
    inline uint32_t rd32 (const uint8_t* p) { return ((uint32_t) p[0] << 24) | ((uint32_t) p[1] << 16) | ((uint32_t) p[2] << 8) | p[3]; }
    inline uint64_t rd64 (const uint8_t* p) { return ((uint64_t) rd32 (p) << 32) | rd32 (p + 4); }

    // reads a padded string at pos; false if it runs off the end
    inline bool rdstr (const uint8_t* d, size_t n, size_t& pos, std::string& out)
    {
        size_t e = pos;
        while (e < n && d[e] != 0) ++e;
        if (e >= n) return false;
        out.assign ((const char*) d + pos, e - pos);
        pos = (e + 4) & ~(size_t) 3;
        return pos <= n;
    }
}

inline std::vector<uint8_t> encode (const Message& m)
{
    std::vector<uint8_t> b;
    b.reserve (64 + m.path.size());
    detail::str (b, m.path);
    std::string tags = ",";
    for (const auto& a : m.args) tags += (a.type == 'f' || a.type == 's' || a.type == 'b') ? a.type : 'i';
    detail::str (b, tags);
    for (const auto& a : m.args)
    {
        switch (a.type)
        {
            case 'f': { uint32_t u; std::memcpy (&u, &a.f, 4); detail::be32 (b, u); break; }
            case 's': detail::str (b, a.s); break;
            case 'b': detail::be32 (b, (uint32_t) a.s.size()); b.insert (b.end(), a.s.begin(), a.s.end()); detail::pad (b); break;
            default:  detail::be32 (b, (uint32_t) a.i); break;
        }
    }
    return b;
}

// one message; false if the packet is malformed
inline bool decodeMessage (const uint8_t* d, size_t n, Message& m)
{
    m = Message {};
    size_t pos = 0;
    if (n < 4 || d[0] != '/') return false;
    if (! detail::rdstr (d, n, pos, m.path)) return false;
    if (pos >= n) return true;                         // no type tag: no arguments (OSC 1.0 allows it)
    std::string tags;
    if (! detail::rdstr (d, n, pos, tags) || tags.empty() || tags[0] != ',') return false;
    for (size_t k = 1; k < tags.size(); ++k)
    {
        Arg a;
        a.type = tags[k];
        switch (tags[k])
        {
            case 'i': if (pos + 4 > n) return false; a.i = (int32_t) detail::rd32 (d + pos); a.f = (float) a.i; pos += 4; break;
            case 'f': { if (pos + 4 > n) return false; uint32_t u = detail::rd32 (d + pos); std::memcpy (&a.f, &u, 4); a.i = (int32_t) a.f; pos += 4; break; }
            case 'h': { if (pos + 8 > n) return false; a.type = 'i'; a.i = (int32_t) (int64_t) detail::rd64 (d + pos); a.f = (float) a.i; pos += 8; break; }
            case 'd': { if (pos + 8 > n) return false; uint64_t u = detail::rd64 (d + pos); double v; std::memcpy (&v, &u, 8); a.type = 'f'; a.f = (float) v; a.i = (int32_t) v; pos += 8; break; }
            case 's': case 'S': a.type = 's'; if (! detail::rdstr (d, n, pos, a.s)) return false; break;
            case 'b':
            {
                if (pos + 4 > n) return false;
                const uint32_t len = detail::rd32 (d + pos);
                pos += 4;
                if (pos + len > n) return false;
                a.s.assign ((const char*) d + pos, len);
                pos = (pos + len + 3) & ~(size_t) 3;
                break;
            }
            case 'T': a.type = 'i'; a.i = 1; a.f = 1.0f; break;
            case 'F': case 'N': case 'I': a.type = 'i'; break;
            default: return false;                     // unknown size: cannot go on
        }
        m.args.push_back (std::move (a));
    }
    return true;
}

// a packet: one message or a bundle of them (nested bundles too)
inline void decode (const uint8_t* d, size_t n, std::vector<Message>& out, int depth = 0)
{
    if (n >= 16 && std::memcmp (d, "#bundle", 8) == 0)
    {
        if (depth > 4) return;
        size_t pos = 16;                               // "#bundle\0" + timetag
        while (pos + 4 <= n)
        {
            const uint32_t len = detail::rd32 (d + pos);
            pos += 4;
            if (len == 0 || pos + len > n) return;
            decode (d + pos, len, out, depth + 1);
            pos += len;
        }
        return;
    }
    Message m;
    if (decodeMessage (d, n, m)) out.push_back (std::move (m));
}

} // namespace tether::osc
