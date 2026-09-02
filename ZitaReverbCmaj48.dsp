declare name        "ZitaReverbCmaj";
declare description "Faust port of the Cmajor ZitaReverb example patch";
declare version     "1.0";
declare license     "GPLv3";

// Line-by-line port of examples/patches/ZitaReverb/ZitaReverb.cmajor
// (cmajor-lang/cmajor), itself a rewrite of Fons Adriaensen's zita-rev1.
// Same topology, same coefficients, same delay-line sizes (16384) so that
// the memory footprint matches the Cmajor version.

import("stdfaust.lib");

SR = 48000.0;  // specialisation a la compilation

//==============================================================================
// User interface: same names / ranges / defaults as the Cmajor patch
predelayMs = hslider("Delay[unit:ms]",        40,  20,   100, 0.1);
crossover  = hslider("Crossover[unit:hz]",   200,  50,  1000, 1);
rtLow      = hslider("Low RT60[unit:secs]",    3,   1,     8, 0.01);
rtMid      = hslider("Mid RT60[unit:secs]",    2,   1,     8, 0.01);
damping    = hslider("HF Damping[unit:khz]",   6, 1.5,    24, 0.01) * 1000.0;
eq1Freq    = hslider("EQ1 Freq[unit:hz]",    400,  40,  2500, 1);
eq1Gain    = hslider("EQ1 Gain[unit:db]",      0, -15,    15, 0.1);
eq2Freq    = hslider("EQ2 Freq[unit:hz]",   4000, 160, 10000, 1);
eq2Gain    = hslider("EQ2 Gain[unit:db]",      0, -15,    15, 0.1);
mix        = hslider("Mix[unit:%]",           50,   0,   100, 0.1) * 0.01;

//==============================================================================
// processor Delay: predelay. Note the -20ms offset of the original code.
maxPredelay = 19200;   // 100 ms at processor.maxFrequency (192 kHz)
predelaySamples = int(floor((predelayMs * 0.001 - 0.020) * SR + 0.5));
predelay = de.delay(maxPredelay, predelaySamples);

//==============================================================================
// processor ReverbCore
delaySize = 16384;

dp1(i) = ba.take(i+1, ( 20346e-6,  24421e-6,  31604e-6,  27333e-6,
                        22904e-6,  29291e-6,  13458e-6,  19123e-6));
dp2(i) = ba.take(i+1, (153129e-6, 210389e-6, 127837e-6, 256891e-6,
                       174713e-6, 192303e-6, 125000e-6, 219991e-6));
apc(i) = ba.take(i+1, (0.6, -0.6, 0.6, -0.6, 0.6, -0.6, 0.6, -0.6));

k1(i) = int(floor(dp1(i) * SR + 0.5));
k2(i) = int(floor(dp2(i) * SR + 0.5));

// Filter::setParams
wlo = 6.2832 * crossover / SR;
chi = ba.if(damping > 0.49 * SR, 2.0, 1.0 - cos(6.2832 * damping / SR));

gmf(i) = pow(0.001, dp2(i) / rtMid);
glo(i) = pow(0.001, dp2(i) / rtLow) / gmf(i) - 1.0;
fg(i)  = pow(0.001, dp2(i) / (0.5 * rtMid)) / gmf(i);
ft(i)  = (1.0 - fg(i) * fg(i)) / (2.0 * fg(i) * fg(i) * chi);
whi(i) = (sqrt(1.0 + 4.0 * ft(i)) - 1.0) / (2.0 * ft(i));

// one-pole with the denormal-guard offset of the original
onepole(w, eps) = *(w) : +(eps) : + ~ *(1.0 - w);

// Filter::process
filter(i) = (_ <: (_, (onepole(wlo, 1e-10) : *(glo(i)))))
          :> (onepole(whi(i), 0.0) : *(gmf(i)));

// shuffle(): the 12 butterflies of the 8x8 Hadamard, in the original order
bfly = _,_ <: +,-;
p13  = ro.interleave(2,4);              // (0,1)(2,3)(4,5)(6,7) -> stride 2 pairs
swp  = route(8,8, 1,1, 2,3, 3,2, 4,4, 5,5, 6,7, 7,6, 8,8);
p3   = route(8,8, 1,1, 2,3, 3,5, 4,7, 5,2, 6,4, 7,6, 8,8);
p3i  = route(8,8, 1,1, 3,2, 5,3, 7,4, 2,5, 4,6, 6,7, 8,8);

shuffle8 = par(i, 4, bfly)              // (0,1)(2,3)(4,5)(6,7)
         : swp : par(i, 4, bfly) : swp  // (0,2)(1,3)(4,6)(5,7)
         : p3  : par(i, 4, bfly) : p3i; // (0,4)(1,5)(2,6)(3,7)

wetLevel = 1.0 / sqrt(rtMid);
fdnGain  = sqrt(0.125);

// input injection: 0.3*in0 -> +,+,-,- ; 0.3*in1 -> +,+,-,-
inject1 = *(0.3) <: _, _, *(-1), *(-1);
inject  = inject1, inject1;

outs(y0,y1,y2,y3,y4,y5,y6,y7) = wetLevel * (y1 + y2), wetLevel * (y1 - y2);
writes = par(i, 8, *(fdnGain) : filter(i));

fdnbody = (si.bus(8), inject)
        : ro.interleave(8,2) : par(i, 8, +)
        : par(i, 8, fi.allpass_comb(delaySize, k1(i), apc(i)))
        : shuffle8
        : (si.bus(8) <: (writes, outs));

// the ~ operator already introduces the 1-sample loop delay
dlines = par(i, 8, de.delay(delaySize, k2(i) - k1(i) - 1));

reverbCore = (fdnbody ~ dlines) : (si.block(8), si.bus(2));

//==============================================================================
// processor DryWetMixer
dryLevel = (1.0 - mix) * (1.0 + mix);
wetGain  = 0.7 * mix * (2.0 - mix);

//==============================================================================
// processor ParametericEQ (steady state: the "static" branch)
eqG(gdb) = pow(10.0, 0.05 * gdb);

eqSection(gdb, freq) = (body ~ si.bus(2)) : (!,!,_)
with {
    g   = eqG(gdb);
    f   = freq * (ma.PI / SR);
    b   = 2.0 * f / sqrt(g);
    gg  = 0.5 * (g - 1.0);
    c1  = 0.0 - cos(2.0 * f);
    c2  = (1.0 - b) / (1.0 + b);
    body(z1p, z2p, x) = z1n, z2n, res
    with {
        yA  = x - c2 * z2p;
        res = x - gg * (z2p + c2 * yA - x);
        yB  = yA - c1 * z1p;
        z2n = z1p + c1 * yB;
        z1n = yB + 1e-20;
    };
};

eq(gdb, freq) = eqSection(gdb, freq), eqSection(gdb, freq);

//==============================================================================
process = _, _ <: (dry, wet) : ro.interleave(2,2) : par(i, 2, +)
        : eq(eq1Gain, eq1Freq)
        : eq(eq2Gain, eq2Freq)
with {
    dry = par(i, 2, *(dryLevel));
    wet = par(i, 2, predelay) : reverbCore : par(i, 2, *(wetGain));
};
