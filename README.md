# Zita Reverb: Cmajor-generated C++ vs Faust-generated C++

A like-for-like CPU comparison of the same reverb algorithm compiled by two
different DSP compilers.

The reference is the [ZitaReverb example patch][upstream] from `cmajor-lang/cmajor`
— [documented on cmajor.dev][docs], itself a rewrite of Fons Adriaensen's
zita-rev1 — a graph of five processors: predelay, an 8-branch FDN core (allpass +
delay + RT60 shelving filter per branch, 8x8 Hadamard mixing in 12 butterflies),
dry/wet mixer, two parametric EQs.

`ZitaReverbCmaj.dsp` is a line-by-line Faust port of that patch: same topology,
same coefficients, same delay-line sizes. `ZitaReverbCmaj48.dsp` is the same file
with the sample rate fixed at compile time.

[upstream]: https://github.com/cmajor-lang/cmajor/tree/main/examples/patches/ZitaReverb
[docs]: https://cmajor.dev/docs/Examples/ZitaReverb/

## Fidelity

`make check` renders a 96000-frame stereo impulse response from both and compares
them sample by sample. From frame 32 onwards the relative error is **1.8e-7
(-135 dB)** — float rounding, nothing else.

Only the very first sample differs: Cmajor ramps its dry/wet levels over one
32-frame block on the first parameter change; the Faust port applies the value
directly. Three control-rate behaviours are deliberately not ported:

- the per-block linear ramps of the dry/wet and reverb levels;
- the EQ's bypass / static / smooth state machine — Cmajor computes nothing at
  all when the gain is 0 dB, Faust always computes the section;
- Faust recomputes the eight filters' `pow`/`sqrt` on every `compute()` call,
  Cmajor only when a parameter moved.

None of these change the steady state; they change what happens while a knob moves.

## Results

Apple M5, `clang++ -std=c++17 -O3 -mcpu=apple-m4` on both sides, 48 kHz, block of
512, single precision. EQ gains set to +6/-6 dB so that both engines actually run
the whole chain.

| Implementation                              | ns/frame | % of one core | ratio |
|---------------------------------------------|---------:|--------------:|------:|
| Cmajor -> C++, power-of-two predelay         |    39.5  |         0.189 |  1.00 |
| Cmajor -> C++ (`cmaj generate`)              |    39.3  |         0.189 |  1.00 |
| Faust -> C++ `-lang cpp -vec -lv 0 -vs 32`   |    26.0  |         0.125 |  1.51 |
| Faust -> C++ `-lang cpp` (default)           |    24.6  |         0.118 |  1.60 |
| Cmajor -> native LLVM JIT (-O4)              |   ~ 22.5 |       ~ 0.108 | ~1.75 |
| Faust -> C++ `-lang cpp`, SR fixed at 48 kHz |    21.8  |         0.105 |  1.80 |
| Faust -> LLVM JIT (libfaust)                 |    21.1  |         0.101 |  1.86 |
| Faust -> C++ `-lang cpp -mcd 0`              |    19.0  |         0.091 |  2.07 |
| **Faust -> C++ `-lang ocpp -lsum`**          | **11.6** |     **0.056** |**3.40**|

The last row is the option set elected by `fcautotool` (`make autotune`), which
races the supported candidate flag sets against each other and gates the winner on
reproducing the reference impulse response. Its own verdict on this DSP:

```
  lb        10.1730 ns  (-lang ocpp -lsum)
  fibfu     11.3210 ns  (-lang ocpp -fir -iirt -lsum -ls-fuse -ls-sched model)
  al        14.0150 ns  (-lang ocpp -ss 8)
  ...
  cppmcd0   19.4140 ns  (-lang cpp -mcd 0)
  cppvec    21.1010 ns  (-lang cpp -vec)
  cpp       23.2110 ns  (-lang cpp)
```

Two of those candidates are worth keeping in mind on their own. `-mcd 0` stays on
the default `cpp` backend and only forces every delay, however short, into a ring
buffer instead of the copy-shift lines Faust emits below the `-mcd` threshold: it
costs nothing in memory (identical 1.39 MB state) and takes the default 24.6 ns
down to 19.0. Going further means changing backend: `-lang ocpp` alone measures
16.4 ns here, and `-lang ocpp -lsum` 11.6 ns.

`-lsum` is flagged experimental in `faust -h`; its output was checked against the
reference all the same (-135.1 dB with the EQs bypassed, -86.3 dB with them
active, i.e. the same figures as the default backend). `-mcd 0` was checked the
same way and lands at -135.0 dB, bit-comparable to the default build.

At the patch's shipped defaults (EQ gains at 0 dB) Cmajor short-circuits both EQ
processors and drops to 35.4 ns; Faust stays put, so the ratios become 1.43x
(`-lang cpp`), 1.85x (`-mcd 0`) and 3.05x (`-lang ocpp -lsum`). The ratio is
stable from 32 to 1024 frames per block.

State footprint: 1.21 MB for Cmajor, 1.39 MB for the generic Faust port, 0.45 MB
for the fixed-rate variant.

## Where the gap comes from

Cmajor's **C++ backend** unrolls the graph one frame at a time: the generated
`advance()` calls a `main()` that zeroes five intermediate IO structs, accumulates
them between nodes, then enters each processor through a `switch (_resumeIndex)` —
the translation of the language's `loop`/`advance()` coroutines. The C++ compiler
cannot fuse that into a single loop. Faust emits one fused loop over flat state
arrays, with slider-dependent coefficients hoisted out of it.

This is not a verdict on the Cmajor language: its native LLVM engine runs at about
22.5 ns/frame, level with the best Faust figure. The measured gap is the C++
*code generator*, not the Cmajor compiler.

Side finding: the patch's predelay buffer is 19200 words, not a power of two, so
every sample pays two integer modulos. Forcing 32768 (`make` builds that variant
too) buys 1.3 ns — 3.6%. The rest of the gap is structural.

## Method

`bench_all.cpp` holds every implementation in one binary. A round processes 10 s
of identical noise per engine, engines alternating *inside* the round so thermal
drift and frequency scaling hit all of them equally; the minimum over 11 rounds is
kept, the first round being warm-up. The thread runs at `QOS_CLASS_USER_INTERACTIVE`
to stay on performance cores.

`make cmaj-jit` estimates Cmajor's native engine differently — the slope of
`cmaj render` wall time against render length, minus the same slope for a
passthrough patch, to subtract start-up and WAV encoding. That figure is worth
about +/-10%, unlike the in-binary numbers.

## Targets

```
make            fetch the upstream patch, generate both C++ versions, build
make run        the interleaved A/B benchmark
make check      impulse-response comparison
make autotune   re-elect the best faust options with fcautotool
make jit        add the libfaust LLVM JIT data point (needs llvm-config)
make cmaj-jit   estimate Cmajor's own LLVM engine
make clean
```

The Cmajor sources are downloaded by the Makefile rather than vendored here: they
are dual GPLv3 / commercial licensed.

`index.html` is a standalone write-up of the same material, published at
https://sletz.github.io/zita-cmajor-faust/

## Licence

GPL-3.0 (`LICENSE`). The Faust port is a derived work of the [Cmajor ZitaReverb
example][docs], which is offered under either GPLv3 or a commercial licence; the
original algorithm is Fons Adriaensen's zita-rev1. The Cmajor sources themselves
are not vendored here — the Makefile downloads them.
