#include "bench_common.h"
#include "cpp/ZitaReverbCmaj.h"

// EQ gains can be forced non-zero so that the ParametericEQ processors are in
// their "static" (active) state instead of the bypass short-circuit.
static float g_eq1Gain = 0.0f, g_eq2Gain = 0.0f;

static void setParams (ZitaReverb& p)
{
    p.addEvent_delayIn     (40.0f);
    p.addEvent_crossoverIn (200.0f);
    p.addEvent_rtLowIn     (3.0f);
    p.addEvent_rtMidIn     (2.0f);
    p.addEvent_dampingIn   (6.0f);
    p.addEvent_eq1FreqIn   (400.0f);
    p.addEvent_eq1GainIn   (g_eq1Gain);
    p.addEvent_eq2FreqIn   (4000.0f);
    p.addEvent_eq2GainIn   (g_eq2Gain);
    p.addEvent_mixIn       (50.0f);
}

int main (int argc, char** argv)
{
    std::string mode = argc > 1 ? argv[1] : "bench";
    if (argc > 2) g_eq1Gain = (float) atof (argv[2]);
    if (argc > 3) g_eq2Gain = (float) atof (argv[3]);

    static ZitaReverb proc;
    proc.initialise (0, kSR);
    setParams (proc);

    if (mode == "dump")
    {
        int n = argc > 4 ? atoi (argv[4]) : 8192;
        std::vector<float> in ((size_t) n * 2, 0.0f), out ((size_t) n * 2, 0.0f);
        in[0] = 1.0f; in[1] = 1.0f;                    // stereo impulse
        for (int i = 0; i < n; i += kBlockSize)
        {
            int b = std::min (kBlockSize, n - i);
            proc.setInputFrames (11, in.data() + (size_t) i * 2, (uint32_t) b, 0);
            proc.advance (b);
            proc.copyOutputFrames (12, out.data() + (size_t) i * 2, (uint32_t) b);
        }
        FILE* f = fopen ("ir_cmaj.raw", "wb");
        fwrite (out.data(), 4, out.size(), f);
        fclose (f);
        printf ("dumped %d frames\n", n);
        return 0;
    }

    // ---- benchmark -----------------------------------------------------
    const int seconds = 20;
    const size_t frames = (size_t) (kSR * seconds);
    auto noise = makeNoise (frames * 2);
    std::vector<float> out ((size_t) kBlockSize * 2);

    Result best { 1e30, 0 };
    for (int rep = 0; rep < 5; ++rep)
    {
        double chk = 0;
        auto t0 = std::chrono::steady_clock::now();
        for (size_t i = 0; i < frames; i += kBlockSize)
        {
            proc.setInputFrames (11, noise.data() + i * 2, (uint32_t) kBlockSize, 0);
            proc.advance (kBlockSize);
            proc.copyOutputFrames (12, out.data(), (uint32_t) kBlockSize);
            chk += out[0] + out[2 * kBlockSize - 1];
        }
        auto t1 = std::chrono::steady_clock::now();
        double ns = std::chrono::duration<double, std::nano> (t1 - t0).count() / (double) frames;
        if (ns < best.nsPerFrame) best = { ns, chk };
    }
    report ("Cmajor -> C++ (cmaj generate)", best, (double) frames);
    return 0;
}
