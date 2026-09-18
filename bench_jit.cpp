// Faust LLVM-JIT (default and -mcd 0) vs C++, in one interleaved run.
#include "bench_common.h"
#include <pthread.h>
#include <map>
#include <string>
#include "faust/dsp/llvm-dsp.h"
#include "faust/gui/UI.h"
#include "v_scal.h"

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

static void setP (ZoneUI& ui, float e1, float e2)
{
    ui.set ("Delay", 40);  ui.set ("Crossover", 200); ui.set ("Low RT60", 3);
    ui.set ("Mid RT60", 2); ui.set ("HF Damping", 6); ui.set ("EQ1 Freq", 400);
    ui.set ("EQ1 Gain", e1); ui.set ("EQ2 Freq", 4000); ui.set ("EQ2 Gain", e2);
    ui.set ("Mix", 50);
}

int main (int argc, char** argv)
{
    float e1 = argc > 1 ? atof (argv[1]) : 0, e2 = argc > 2 ? atof (argv[2]) : 0;
    pthread_set_qos_class_self_np (QOS_CLASS_USER_INTERACTIVE, 0);

    std::string err;
    const char* args[] = { "-single" };
    auto* factory = createDSPFactoryFromFile ("ZitaReverbCmaj.dsp", 1, args, "", err, -1);
    if (! factory) { printf ("JIT error: %s\n", err.c_str()); return 1; }
    const char* mcd0Args[] = { "-single", "-mcd", "0" };
    auto* mcd0Factory = createDSPFactoryFromFile ("ZitaReverbCmaj.dsp", 3, mcd0Args, "", err, -1);
    if (! mcd0Factory)
    {
        printf ("JIT -mcd 0 error: %s\n", err.c_str());
        deleteDSPFactory (factory);
        return 1;
    }
    dsp* jit = factory->createDSPInstance();
    dsp* jitMcd0 = mcd0Factory->createDSPInstance();
    if (! jit || ! jitMcd0)
    {
        printf ("JIT instance creation failed\n");
        delete jit;
        delete jitMcd0;
        deleteDSPFactory (factory);
        deleteDSPFactory (mcd0Factory);
        return 1;
    }
    jit->init ((int) kSR);
    ZoneUI uij; jit->buildUserInterface (&uij); setP (uij, e1, e2);

    jitMcd0->init ((int) kSR);
    ZoneUI uim; jitMcd0->buildUserInterface (&uim); setP (uim, e1, e2);

    static zitaFaustScal cpp; cpp.init ((int) kSR);
    ZoneUI uic; cpp.buildUserInterface (&uic); setP (uic, e1, e2);

    const size_t frames = (size_t) (kSR * 10);
    const int block = 512, rounds = 12;
    auto noiseI = makeNoise (frames * 2);
    std::vector<float> nL (frames), nR (frames), oL (block), oR (block);
    for (size_t i = 0; i < frames; ++i) { nL[i] = noiseI[2*i]; nR[i] = noiseI[2*i+1]; }

    dsp* engines[] = { jit, jitMcd0, &cpp };
    const char* labels[] = {
        "Faust -> LLVM JIT (libfaust)",
        "Faust -> LLVM JIT (-mcd 0)",
        "Faust -> C++ scalar (meme binaire)"
    };
    double best[] = { 1e30, 1e30, 1e30 };
    double sink = 0;
    for (int r = 0; r < rounds; ++r)
    {
        for (int which = 0; which < 3; ++which)
        {
            auto t0 = std::chrono::steady_clock::now();
            for (size_t i = 0; i + block <= frames; i += block)
            {
                float* in[2] = { nL.data() + i, nR.data() + i };
                float* out[2] = { oL.data(), oR.data() };
                engines[which]->compute (block, in, out);
                sink += oL[0];
            }
            auto t1 = std::chrono::steady_clock::now();
            double ns = std::chrono::duration<double,std::nano> (t1-t0).count() / (double) frames;
            if (r > 0) best[which] = std::min (best[which], ns);
        }
    }
    for (int which = 0; which < 3; ++which)
        printf ("%-34s %7.2f ns/frame  %6.3f %% CPU\n",
                labels[which], best[which], best[which]*kSR*1e-9*100);
    if (sink == 1e300) printf("!");

    // release the JIT instance and its factory before libfaust's own globals
    // are torn down at exit, which otherwise aborts on a dead recursive_mutex
    delete jit;
    delete jitMcd0;
    deleteDSPFactory (factory);
    deleteDSPFactory (mcd0Factory);
    return 0;
}
