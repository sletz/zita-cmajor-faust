#pragma once
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#include <algorithm>

static constexpr double kSR        = 48000.0;
static constexpr int    kBlockSize = 512;

// deterministic pseudo-noise, identical for every engine under test
inline std::vector<float> makeNoise (size_t n, uint32_t seed = 12345)
{
    std::vector<float> v (n);
    uint32_t s = seed;
    for (size_t i = 0; i < n; ++i)
    {
        s = s * 1664525u + 1013904223u;
        v[i] = ((float) (s >> 8) * (1.0f / 8388608.0f) - 1.0f) * 0.25f;
    }
    return v;
}

struct Result { double nsPerFrame; double checksum; };

inline void report (const char* name, Result r, double frames)
{
    double cpu = r.nsPerFrame * kSR * 1e-9 * 100.0;   // % of one core at 48 kHz
    double mbps = (frames / (r.nsPerFrame * 1e-9 * frames)) * 4.0 * 2.0 / 1e6; // out MB/s
    printf ("%-34s  %8.2f ns/frame   %7.3f %% CPU @48k   %8.1f x realtime   %8.1f MB/s   [chk %.6f]\n",
            name, r.nsPerFrame, cpu, 1e9 / (r.nsPerFrame * kSR), mbps, r.checksum);
}
