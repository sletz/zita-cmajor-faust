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
the whole chain. Run of 2026-09-18 (Faust 2.89.0, Cmajor 1.0.3177, Apple clang
21; the upstream patch re-fetched that day); the 2026-09-18 morning run and the
original one are in `results.txt`.

| Implementation                              | ns/frame | % of one core | ratio |
|---------------------------------------------|---------:|--------------:|------:|
| Cmajor -> C++, power-of-two predelay         |    37.7  |         0.181 |  1.00 |
| Cmajor -> C++ (`cmaj generate`)              |    37.5  |         0.180 |  1.00 |
| Faust -> C++ `-lang cpp -vec -lv 0 -vs 32`   |    25.7  |         0.123 |  1.46 |
| Faust -> C++ `-lang cpp` (default)           |    23.6  |         0.113 |  1.59 |
| Faust -> C++ `-lang cpp`, SR fixed at 48 kHz |    23.6  |         0.113 |  1.59 |
| Faust -> LLVM JIT (libfaust)                 |    20.6  |         0.099 |  1.82 |
| Cmajor -> native LLVM JIT (-O4)              |  ~ 19.8  |       ~ 0.095 | ~1.9  |
| Faust -> C++ `-lang cpp -mcd 0`              |    16.0  |         0.077 |  2.34 |
| Faust -> LLVM JIT `-mcd 0`                   |    15.5  |         0.074 |  2.42 |
| **Faust -> C++ `-lang ocpp -lsum`**          | **11.2** |     **0.054** |**3.34**|

The C++ rows are one `make run`; the two libfaust rows are the `make jit` run
detailed below, and the Cmajor JIT row is the median of three `make cmaj-jit`
estimates (18.1, 19.8, 28.5 ns/frame: that method is far noisier than the
in-binary numbers, see Method). Ratios are against the 37.5 ns/frame Cmajor C++
baseline of the same run.

Against the original run (`results.txt`), the Cmajor C++ side gained 5 % (39.3
to 37.5), the Faust default 4 % (24.6 to 23.6), `-mcd 0` 16 % (19.0 to 16.0),
and the fixed-rate variant lost its edge: it now measures the same 23.6 ns as
the generic one, where it was 21.8 against 24.6.

The last row is the option set elected by `fcautotool` (`make autotune`), which
races the supported candidate flag sets against each other and gates the winner on
reproducing the reference impulse response. Its verdict on this DSP, 2026-09-18
(two runs, 39 and 42 s):

```
  fib       11.2830 ns  (-lang ocpp -fir -iirt -lsum)
  lb        11.3090 ns  (-lang ocpp -lsum)
  fibfu     11.6880 ns  (-lang ocpp -fir -iirt -lsum -ls-fuse -ls-sched model)
  fibmx     11.6920 ns  (-lang ocpp -fir -iirt -lsum -mxr -ls-fuse -ls-sched model)
  h2        13.6020 ns  (-lang ocpp -ss 9 -ls-R 2 -ls-U 4)
  ...
  df        15.3700 ns  (-lang ocpp)
  cppmcd0   15.3960 ns  (-lang cpp -mcd 0)
  ...
  cpp       20.9120 ns  (-lang cpp)
  cppvec    21.8120 ns  (-lang cpp -vec)
```

`fib` wins both runs (11.175 and 11.283 ns) over `lb` by 0.03 to 0.1 ns, under
1 %, which the jury's own spread covers: `-fir -iirt` (the FIR and transposed
IIR rewrites) add nothing measurable to `-lsum` on this reverb. The benchmarked
row keeps `-lang ocpp -lsum`, the set elected in the original run (`lb` then
won at 10.17 ns, `fibfu` second at 11.32).

Two of those candidates are worth keeping in mind on their own. `-mcd 0` stays on
the default `cpp` backend and only forces every delay, however short, into a ring
buffer instead of the copy-shift lines Faust emits below the `-mcd` threshold: it
costs nothing in memory (identical 1.39 MB state) and takes the default 23.6 ns
down to 16.0. Going further means changing backend: `-lang ocpp` alone measured
16.4 ns in the original run, and `-lang ocpp -lsum` 11.2 ns today.

`-lsum` is flagged experimental in `faust -h`; its output was checked against the
reference all the same (-135.1 dB with the EQs bypassed, -86.3 dB with them
active, i.e. the same figures as the default backend). `-mcd 0` was checked the
same way and lands at -135.0 dB, bit-comparable to the default build.

At the patch's shipped defaults (EQ gains at 0 dB) Cmajor short-circuits both EQ
processors and drops to 34.1 ns; Faust stays put, so the ratios become 1.42x
(`-lang cpp`), 2.12x (`-mcd 0`) and 3.00x (`-lang ocpp -lsum`). The ratio is
stable from 32 to 1024 frames per block.

State footprint: 1.21 MB for Cmajor, 1.39 MB for the generic Faust port, 0.45 MB
for the fixed-rate variant.

### LLVM JIT with `-mcd 0`

The `make jit` run of 2026-09-18 (afternoon) measured the following three
variants interleaved in the same process, at 48 kHz, 512 frames per block,
single precision and EQ gains of +6/-6 dB. The two JIT results are the ones in
the main table above.

| Implementation                       | ns/frame | % of one core |
|--------------------------------------|---------:|--------------:|
| Faust -> LLVM JIT (libfaust, default) |    20.59 |         0.099 |
| Faust -> LLVM JIT `-mcd 0`            |    15.50 |         0.074 |
| Faust -> C++ scalar (same binary)     |    23.47 |         0.113 |

`-mcd 0` reduces LLVM JIT processing time by **24.7%** in this run
(**1.33x** speedup over the default JIT; the morning's run gave 22.6% and
1.29x, from 21.60 to 16.72 ns). Comparing 96000-frame stereo impulse
and deterministic-noise responses, with EQ gains at both 0/0 and +6/-6 dB,
gave a maximum relative RMS error of **2.21e-7** against the default JIT
(morning run).

## Where the gap comes from

Cmajor's **C++ backend** unrolls the graph one frame at a time: the generated
`advance()` calls a `main()` that zeroes five intermediate IO structs, accumulates
them between nodes, then enters each processor through a `switch (_resumeIndex)` —
the translation of the language's `loop`/`advance()` coroutines. The C++ compiler
cannot fuse that into a single loop. Faust emits one fused loop over flat state
arrays, with slider-dependent coefficients hoisted out of it.

This is not a verdict on the Cmajor language: its native LLVM engine runs at
about 20 ns/frame (18.1, 19.8 and 28.5 in three estimates of 2026-09-18, 22.5
originally), level with the Faust LLVM JIT. The measured gap is the C++
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

`make jit` interleaves three variants in the same process: the default libfaust
LLVM JIT, LLVM JIT with `-mcd 0`, and scalar C++. Both JIT factories use
single precision and the same LLVM optimization level, inputs and parameters.
It reports each variant separately after one warm-up round; the minimum of the
remaining 11 rounds is kept. `-mcd 0` forces ring buffers for short delays in
this LLVM measurement as well as in the C++ variant above.

`make cmaj-jit` estimates Cmajor's native engine differently — the slope of
`cmaj render` wall time against render length, minus the same slope for a
passthrough patch, to subtract start-up and WAV encoding. That figure is far
noisier than the in-binary numbers: three consecutive estimates on 2026-09-18
gave 28.5, 18.1 and 19.8 ns/frame. Run it several times and keep the median.

## Targets

```
make            fetch the upstream patch, generate both C++ versions, build
make run        the interleaved A/B benchmark
make check      impulse-response comparison
make autotune   re-elect the best faust options with fcautotool
make jit        compare libfaust LLVM JIT default and -mcd 0 with C++ (needs llvm-config)
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
