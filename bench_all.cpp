// Interleaved A/B benchmark: Cmajor-generated C++ vs Faust-generated C++,
// same algorithm, same compiler, same flags, same machine, same round.
#include "bench_common.h"
#include <functional>
#include <pthread.h>
#include <map>

#include "cpp/ZitaReverbCmaj.h"
namespace p2 {
#include "cpp/ZitaReverbCmajP2.h"
}

#include "faust/dsp/dsp.h"
#include "faust/gui/UI.h"
#include "faust/gui/meta.h"
#include "v_scal.h"
#undef FAUSTCLASS
#include "v_vec.h"
#undef FAUSTCLASS
#include "v_48.h"
#undef FAUSTCLASS
#include "v_ocpp.h"

struct ZoneUI : public UI
{
    std::map<std::string, FAUSTFLOAT*> zones;
    void openTabBox (const char*) override {}
    void openHorizontalBox (const char*) override {}
    void openVerticalBox (const char*) override {}
    void closeBox() override {}
    void addButton (const char* l, FAUSTFLOAT* z) override { zones[l] = z; }
    void addCheckButton (const char* l, FAUSTFLOAT* z) override { zones[l] = z; }
    void addVerticalSlider (const char* l, FAUSTFLOAT* z, FAUSTFLOAT, FAUSTFLOAT, FAUSTFLOAT, FAUSTFLOAT) override { zones[l] = z; }
    void addHorizontalSlider (const char* l, FAUSTFLOAT* z, FAUSTFLOAT, FAUSTFLOAT, FAUSTFLOAT, FAUSTFLOAT) override { zones[l] = z; }
    void addNumEntry (const char* l, FAUSTFLOAT* z, FAUSTFLOAT, FAUSTFLOAT, FAUSTFLOAT, FAUSTFLOAT) override { zones[l] = z; }
    void addHorizontalBargraph (const char* l, FAUSTFLOAT* z, FAUSTFLOAT, FAUSTFLOAT) override { zones[l] = z; }
    void addVerticalBargraph (const char* l, FAUSTFLOAT* z, FAUSTFLOAT, FAUSTFLOAT) override { zones[l] = z; }
    void addSoundfile (const char*, const char*, Soundfile**) override {}
    void declare (FAUSTFLOAT*, const char*, const char*) override {}
    void set (const char* l, float v) { auto it = zones.find (l); if (it != zones.end()) *it->second = v; }
};

static float g_eq1 = 0.0f, g_eq2 = 0.0f;

template <typename T> static void setupFaust (T& d, ZoneUI& ui)
{
    d.init ((int) kSR);
    d.buildUserInterface (&ui);
    ui.set ("Delay", 40);        ui.set ("Crossover", 200);
    ui.set ("Low RT60", 3);      ui.set ("Mid RT60", 2);
    ui.set ("HF Damping", 6);    ui.set ("EQ1 Freq", 400);
    ui.set ("EQ1 Gain", g_eq1);  ui.set ("EQ2 Freq", 4000);
    ui.set ("EQ2 Gain", g_eq2);  ui.set ("Mix", 50);
}

int main (int argc, char** argv)
{
    if (argc > 1) g_eq1 = (float) atof (argv[1]);
    if (argc > 2) g_eq2 = (float) atof (argv[2]);
    int block = argc > 3 ? atoi (argv[3]) : kBlockSize;
    pthread_set_qos_class_self_np (QOS_CLASS_USER_INTERACTIVE, 0);   // performance cores

    const size_t frames = (size_t) (kSR * 10);           // 10 s of audio per round
    const int    rounds = 12;

    auto noiseI = makeNoise (frames * 2);
    std::vector<float> nL (frames), nR (frames);
    for (size_t i = 0; i < frames; ++i) { nL[i] = noiseI[2*i]; nR[i] = noiseI[2*i+1]; }
    std::vector<float> outI ((size_t) block * 2), oL (block), oR (block);

    static ZitaReverb cmaj;
    cmaj.initialise (0, kSR);
    cmaj.addEvent_delayIn (40.f);   cmaj.addEvent_crossoverIn (200.f);
    cmaj.addEvent_rtLowIn (3.f);    cmaj.addEvent_rtMidIn (2.f);
    cmaj.addEvent_dampingIn (6.f);  cmaj.addEvent_eq1FreqIn (400.f);
    cmaj.addEvent_eq1GainIn (g_eq1); cmaj.addEvent_eq2FreqIn (4000.f);
    cmaj.addEvent_eq2GainIn (g_eq2); cmaj.addEvent_mixIn (50.f);

    static p2::ZitaReverb cmaj2;
    cmaj2.initialise (0, kSR);
    cmaj2.addEvent_delayIn (40.f);   cmaj2.addEvent_crossoverIn (200.f);
    cmaj2.addEvent_rtLowIn (3.f);    cmaj2.addEvent_rtMidIn (2.f);
    cmaj2.addEvent_dampingIn (6.f);  cmaj2.addEvent_eq1FreqIn (400.f);
    cmaj2.addEvent_eq1GainIn (g_eq1); cmaj2.addEvent_eq2FreqIn (4000.f);
    cmaj2.addEvent_eq2GainIn (g_eq2); cmaj2.addEvent_mixIn (50.f);

    static zitaFaustScal fscal; static ZoneUI ui1; setupFaust (fscal, ui1);
    static zitaFaustVec  fvec;  static ZoneUI ui2; setupFaust (fvec,  ui2);
    static zitaFaust48   f48;   static ZoneUI ui3; setupFaust (f48,   ui3);
    static zitaOcpp      focpp; static ZoneUI ui4; setupFaust (focpp, ui4);

    struct Engine { const char* name; std::function<double(size_t)> run; double best; };
    std::vector<Engine> engines;

    engines.push_back ({ "Cmajor -> C++  (cmaj generate)", [&] (size_t i)
    {
        cmaj.setInputFrames (11, noiseI.data() + i * 2, (uint32_t) block, 0);
        cmaj.advance (block);
        cmaj.copyOutputFrames (12, outI.data(), (uint32_t) block);
        return (double) (outI[0] + outI[2 * block - 1]);
    }, 1e30 });

    engines.push_back ({ "Cmajor -> C++  (predelai en 2^n)", [&] (size_t i)
    {
        cmaj2.setInputFrames (11, noiseI.data() + i * 2, (uint32_t) block, 0);
        cmaj2.advance (block);
        cmaj2.copyOutputFrames (12, outI.data(), (uint32_t) block);
        return (double) (outI[0] + outI[2 * block - 1]);
    }, 1e30 });

    engines.push_back ({ "Faust  -> C++  (scalar)", [&] (size_t i)
    {
        float* in[2]  = { nL.data() + i, nR.data() + i };
        float* out[2] = { oL.data(), oR.data() };
        fscal.compute (block, in, out);
        return (double) (oL[0] + oR[block - 1]);
    }, 1e30 });

    engines.push_back ({ "Faust  -> C++  (-vec -lv 0 -vs 32)", [&] (size_t i)
    {
        float* in[2]  = { nL.data() + i, nR.data() + i };
        float* out[2] = { oL.data(), oR.data() };
        fvec.compute (block, in, out);
        return (double) (oL[0] + oR[block - 1]);
    }, 1e30 });

    engines.push_back ({ "Faust  -> C++  (scalar, SR fixe 48k)", [&] (size_t i)
    {
        float* in[2]  = { nL.data() + i, nR.data() + i };
        float* out[2] = { oL.data(), oR.data() };
        f48.compute (block, in, out);
        return (double) (oL[0] + oR[block - 1]);
    }, 1e30 });

    engines.push_back ({ "Faust  -> C++  (-lang ocpp, elu)", [&] (size_t i)
    {
        float* in[2]  = { nL.data() + i, nR.data() + i };
        float* out[2] = { oL.data(), oR.data() };
        focpp.compute (block, in, out);
        return (double) (oL[0] + oR[block - 1]);
    }, 1e30 });

    double sink = 0;
    for (int r = 0; r < rounds; ++r)
        for (auto& e : engines)
        {
            auto t0 = std::chrono::steady_clock::now();
            for (size_t i = 0; i + (size_t) block <= frames; i += (size_t) block)
                sink += e.run (i);
            auto t1 = std::chrono::steady_clock::now();
            double ns = std::chrono::duration<double, std::nano> (t1 - t0).count() / (double) frames;
            if (r > 0 && ns < e.best) e.best = ns;      // round 0 = warm-up
        }

    printf ("\n--- block = %d frames, SR = 48 kHz, EQ gains = %+.1f / %+.1f dB, %d rounds of 10 s ---\n",
            block, g_eq1, g_eq2, rounds);
    double ref = engines[0].best;
    for (auto& e : engines)
        printf ("%-36s %7.2f ns/frame  %6.3f %% CPU  %6.0f x RT   %s%.2fx\n",
                e.name, e.best, e.best * kSR * 1e-9 * 100.0, 1e9 / (e.best * kSR),
                (&e == &engines[0] ? "ref " : ""), ref / e.best);
    if (sink == 12345.678) printf ("!");
    return 0;
}
