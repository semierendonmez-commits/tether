// A small animated-GIF writer for the norns screen: 16-colour palette, LZW, one delay per frame,
// looping. No JUCE. Frames are palette indices (0..15), w * h bytes each, already scaled.
#pragma once

#include <array>
#include <cstdint>
#include <cstdio>
#include <string>
#include <unordered_map>
#include <vector>

namespace tether::gif
{

struct Rgb { uint8_t r = 0, g = 0, b = 0; };

class Writer
{
public:
    Writer (int width, int height, const std::array<Rgb, 16>& palette, bool loop = true) : w (width), h (height)
    {
        put ("GIF89a", 6);
        u16 ((uint16_t) w); u16 ((uint16_t) h);
        out.push_back (0xF3);                 // global table, 8-bit colour resolution, 16 entries (2^(3+1))
        out.push_back (0);                    // background index
        out.push_back (0);                    // aspect
        for (const auto& c : palette) { out.push_back (c.r); out.push_back (c.g); out.push_back (c.b); }
        if (loop)
        {
            const uint8_t app[] = { 0x21, 0xFF, 0x0B, 'N', 'E', 'T', 'S', 'C', 'A', 'P', 'E', '2', '.', '0', 0x03, 0x01, 0x00, 0x00, 0x00 };
            put (app, sizeof (app));
        }
    }

    // one frame, shown for delayCs hundredths of a second
    void frame (const uint8_t* idx, int delayCs)
    {
        const uint8_t gce[] = { 0x21, 0xF9, 0x04, 0x04, (uint8_t) (delayCs & 0xFF), (uint8_t) (delayCs >> 8), 0x00, 0x00 };
        put (gce, sizeof (gce));
        out.push_back (0x2C);
        u16 (0); u16 (0); u16 ((uint16_t) w); u16 ((uint16_t) h);
        out.push_back (0);                    // no local table, not interlaced
        lzw (idx, (size_t) w * (size_t) h);
    }

    const std::vector<uint8_t>& finish()
    {
        if (! done) { out.push_back (0x3B); done = true; }
        return out;
    }

    bool save (const std::string& path)
    {
        finish();
        FILE* f = std::fopen (path.c_str(), "wb");
        if (f == nullptr) return false;
        const bool ok = std::fwrite (out.data(), 1, out.size(), f) == out.size();
        return std::fclose (f) == 0 && ok;
    }

private:
    void put (const void* p, size_t n) { auto* b = (const uint8_t*) p; out.insert (out.end(), b, b + n); }
    void u16 (uint16_t v) { out.push_back ((uint8_t) (v & 0xFF)); out.push_back ((uint8_t) (v >> 8)); }

    // LZW with a 4-bit minimum code size, codes packed LSB first into 255-byte sub-blocks
    void lzw (const uint8_t* data, size_t n)
    {
        constexpr int minBits = 4, clear = 1 << minBits, eoi = clear + 1;
        out.push_back ((uint8_t) minBits);
        std::vector<uint8_t> bytes;
        uint32_t acc = 0;
        int accBits = 0, bits = minBits + 1, next = eoi + 1;
        auto emit = [&] (int code)
        {
            acc |= (uint32_t) code << accBits;
            accBits += bits;
            while (accBits >= 8) { bytes.push_back ((uint8_t) (acc & 0xFF)); acc >>= 8; accBits -= 8; }
        };
        std::unordered_map<uint32_t, int> dict;
        dict.reserve (8192);
        emit (clear);
        int prefix = data[0] & 15;
        for (size_t i = 1; i < n; ++i)
        {
            const int c = data[i] & 15;
            const uint32_t key = ((uint32_t) prefix << 8) | (uint32_t) c;
            auto it = dict.find (key);
            if (it != dict.end()) { prefix = it->second; continue; }
            emit (prefix);
            if (next < 4096)
            {
                dict.emplace (key, next++);
                if (next > (1 << bits) && bits < 12) ++bits;
            }
            else
            {
                emit (clear);
                dict.clear();
                bits = minBits + 1;
                next = eoi + 1;
            }
            prefix = c;
        }
        emit (prefix);
        emit (eoi);
        if (accBits > 0) bytes.push_back ((uint8_t) (acc & 0xFF));
        for (size_t p = 0; p < bytes.size(); p += 255)
        {
            const size_t len = std::min<size_t> (255, bytes.size() - p);
            out.push_back ((uint8_t) len);
            out.insert (out.end(), bytes.begin() + (long) p, bytes.begin() + (long) (p + len));
        }
        out.push_back (0);
    }

    int w, h;
    bool done = false;
    std::vector<uint8_t> out;
};

// norns frame (128 x 64 levels) -> palette indices at an integer scale
inline void scaleFrame (const uint8_t* src, int scale, std::vector<uint8_t>& dst)
{
    const int W = 128 * scale, H = 64 * scale;
    dst.resize ((size_t) W * (size_t) H);
    for (int y = 0; y < H; ++y)
        for (int x = 0; x < W; ++x)
            dst[(size_t) (y * W + x)] = (uint8_t) (src[(y / scale) * 128 + x / scale] & 15);
}

} // namespace tether::gif
