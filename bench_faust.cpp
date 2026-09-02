#include "bench_common.h"
#include <map>
#include "faust/dsp/dsp.h"
#include "faust/gui/UI.h"
#include "faust/gui/meta.h"
#include FAUST_DSP_HEADER

// minimal UI: collect the slider zones by label
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

int main (int argc, char** argv)
{
    std::string mode = argc > 1 ? argv[1] : "bench";
    float eq1Gain = argc > 2 ? (float) atof (argv[2]) : 0.0f;
    float eq2Gain = argc > 3 ? (float) atof (argv[3]) : 0.0f;

    static FAUSTCLASS dsp;
    dsp.init ((int) kSR);
    ZoneUI ui; dsp.buildUserInterface (&ui);
    ui.set ("Delay", 40);      ui.set ("Crossover", 200);
    ui.set ("Low RT60", 3);    ui.set ("Mid RT60", 2);
    ui.set ("HF Damping", 6);  ui.set ("EQ1 Freq", 400);
    ui.set ("EQ1 Gain", eq1Gain); ui.set ("EQ2 Freq", 4000);
    ui.set ("EQ2 Gain", eq2Gain); ui.set ("Mix", 50);

    if (mode == "dump")
    {
        int n = argc > 4 ? atoi (argv[4]) : 8192;
        std::vector<float> inL (n, 0.f), inR (n, 0.f), outL (n, 0.f), outR (n, 0.f);
        inL[0] = 1.0f; inR[0] = 1.0f;
        for (int i = 0; i < n; i += kBlockSize)
        {
            int b = std::min (kBlockSize, n - i);
            float* ins[2]  = { inL.data() + i,  inR.data() + i };
            float* outs[2] = { outL.data() + i, outR.data() + i };
            dsp.compute (b, ins, outs);
        }
        std::vector<float> il ((size_t) n * 2);
        for (int i = 0; i < n; ++i) { il[2*i] = outL[i]; il[2*i+1] = outR[i]; }
        FILE* f = fopen ("ir_faust.raw", "wb");
        fwrite (il.data(), 4, il.size(), f);
        fclose (f);
        printf ("dumped %d frames\n", n);
        return 0;
    }

    const int seconds = 20;
    const size_t frames = (size_t) (kSR * seconds);
    auto noiseI = makeNoise (frames * 2);
    std::vector<float> nL (frames), nR (frames);
    for (size_t i = 0; i < frames; ++i) { nL[i] = noiseI[2*i]; nR[i] = noiseI[2*i+1]; }
    std::vector<float> oL (kBlockSize), oR (kBlockSize);

    Result best { 1e30, 0 };
    for (int rep = 0; rep < 5; ++rep)
    {
        double chk = 0;
        auto t0 = std::chrono::steady_clock::now();
        for (size_t i = 0; i < frames; i += kBlockSize)
        {
            float* ins[2]  = { nL.data() + i, nR.data() + i };
            float* outs[2] = { oL.data(), oR.data() };
            dsp.compute (kBlockSize, ins, outs);
            chk += oL[0] + oR[kBlockSize - 1];
        }
        auto t1 = std::chrono::steady_clock::now();
        double ns = std::chrono::duration<double, std::nano> (t1 - t0).count() / (double) frames;
        if (ns < best.nsPerFrame) best = { ns, chk };
    }
    report (argc > 5 ? argv[5] : "Faust -> C++ (scalar)", best, (double) frames);
    return 0;
}
