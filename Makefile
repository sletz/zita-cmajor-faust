# Zita Reverb: Cmajor-generated C++ vs Faust-generated C++
#
#   make help       list the available targets

CXX      ?= clang++
CXXFLAGS ?= -std=c++17 -O3 -I/usr/local/include
ARCHFLAG ?= $(shell uname -m | grep -q arm64 && echo -mcpu=apple-m4 || echo -march=native)
FAUST    ?= faust
CMAJ     ?= cmaj
RAW       = https://raw.githubusercontent.com/cmajor-lang/cmajor/main/examples/patches/ZitaReverb

.PHONY: help all run check jit cmaj-jit autotune clean
.DEFAULT_GOAL := all

# self-documenting: every '## ' comment on a target below becomes a help line
help:
	@echo 'Zita Reverb: Cmajor-generated C++ vs Faust-generated C++'
	@echo
	@awk -F ':.*## ' '/^[a-z][a-z-]*:.*## /{printf "  make %-9s %s\n", $$1, $$2}' $(MAKEFILE_LIST)

all: bench_all ## fetch the upstream patch, generate both C++ versions, build

# ---- upstream Cmajor sources (not vendored: dual GPLv3 / commercial) --------
upstream/ZitaReverb.cmajorpatch:
	@mkdir -p upstream
	curl -sSfL -o upstream/ZitaReverb.cmajor      $(RAW)/ZitaReverb.cmajor
	curl -sSfL -o upstream/ZitaReverb.cmajorpatch $(RAW)/ZitaReverb.cmajorpatch

# variant with a power-of-two predelay buffer, to isolate the cost of the
# integer modulo the 19200-word buffer forces on every sample
upstream-p2/ZitaReverb.cmajorpatch: upstream/ZitaReverb.cmajorpatch
	@mkdir -p upstream-p2
	sed 's|let delaySize = int (processor.maxFrequency \* maxDelayLengthMs / 1000.0);|let delaySize = 32768;|' \
	    upstream/ZitaReverb.cmajor > upstream-p2/ZitaReverb.cmajor
	cp upstream/ZitaReverb.cmajorpatch upstream-p2/

cpp/ZitaReverbCmaj.h: upstream/ZitaReverb.cmajorpatch
	$(CMAJ) generate --target=cpp --maxFramesPerBlock=512 --output=$@ upstream/ZitaReverb.cmajorpatch

cpp/ZitaReverbCmajP2.h: upstream-p2/ZitaReverb.cmajorpatch
	$(CMAJ) generate --target=cpp --maxFramesPerBlock=512 --output=$@ upstream-p2/ZitaReverb.cmajorpatch

# ---- Faust variants --------------------------------------------------------
v_scal.h: ZitaReverbCmaj.dsp
	$(FAUST) -lang cpp -cn zitaFaustScal -o $@ $<
v_vec.h: ZitaReverbCmaj.dsp
	$(FAUST) -vec -lv 0 -vs 32 -lang cpp -cn zitaFaustVec -o $@ $<
v_48.h: ZitaReverbCmaj48.dsp
	$(FAUST) -lang cpp -cn zitaFaust48 -o $@ $<
v_mcd0.h: ZitaReverbCmaj.dsp
	$(FAUST) -lang cpp -mcd 0 -cn zitaFaustMcd0 -o $@ $<
# options elected by fcautotool (see the `autotune` target)
OCPP_OPTS ?= -lang ocpp -lsum
v_ocpp.h: ZitaReverbCmaj.dsp
	$(FAUST) $(OCPP_OPTS) -cn zitaOcpp -o $@ $<

GEN = cpp/ZitaReverbCmaj.h cpp/ZitaReverbCmajP2.h v_scal.h v_vec.h v_48.h v_mcd0.h v_ocpp.h

bench_all: bench_all.cpp bench_common.h $(GEN)
	$(CXX) $(CXXFLAGS) $(ARCHFLAG) -o $@ $<

bench_cmaj: bench_cmaj.cpp bench_common.h cpp/ZitaReverbCmaj.h
	$(CXX) $(CXXFLAGS) $(ARCHFLAG) -o $@ $<

bench_faust: bench_faust.cpp bench_common.h v_scal.h
	$(CXX) $(CXXFLAGS) $(ARCHFLAG) -DFAUST_DSP_HEADER='"v_scal.h"' -o $@ $<

bench_jit: bench_jit.cpp bench_common.h v_scal.h
	$(CXX) $(CXXFLAGS) $(ARCHFLAG) -o $@ $< -L/usr/local/lib -lfaustwithllvm \
	  $(shell llvm-config --libs --system-libs --ldflags) \
	  -Wl,-rpath,$(shell llvm-config --libdir) -lz -lncurses -framework CoreFoundation

# ---- targets ---------------------------------------------------------------
run: bench_all ## the interleaved A/B benchmark (the headline numbers)
	@./bench_all 6 -6 512      # EQ active on both sides: same work compared
	@./bench_all 0 0 512       # shipped defaults: Cmajor short-circuits its EQs

check: bench_cmaj bench_faust ## compare the two impulse responses (port fidelity)
	./bench_cmaj dump 0 0 96000
	./bench_faust dump 0 0 96000
	@python3 -c "import numpy as np; \
a=np.fromfile('ir_cmaj.raw',dtype=np.float32).reshape(-1,2)[32:]; \
b=np.fromfile('ir_faust.raw',dtype=np.float32).reshape(-1,2)[32:]; \
r=np.sqrt(((a-b)**2).mean())/np.sqrt((a**2).mean()); \
print('relative error %.3e (%.1f dB)'%(r,20*np.log10(r)))"

# re-elect the best faust options for this DSP (~30 s)
autotune: ## re-elect the best faust options with fcautotool (~30 s)
	FCBENCH_ARCH_FLAGS="$(ARCHFLAG)" fcautotool ZitaReverbCmaj.dsp

jit: bench_jit ## add the libfaust LLVM JIT data point (needs llvm-config)
	@./bench_jit 6 -6

# Cmajor's native LLVM engine: slope of 'cmaj render' minus that of a
# passthrough patch, to subtract start-up and WAV encoding. +/- 10%.
# LC_ALL=C: a comma decimal separator from time(1)/awk breaks the arithmetic
cmaj-jit: export LC_ALL = C
cmaj-jit: upstream/ZitaReverb.cmajorpatch ## estimate the cost of Cmajor's own LLVM engine
	@for p in upstream/ZitaReverb.cmajorpatch Null.cmajorpatch; do \
	  for L in 12000000 60000000; do \
	    m=99; for r in 1 2 3 4 5 6 7; do \
	      t=$$( { /usr/bin/time -p $(CMAJ) render -O4 --engine=llvm --rate=48000 \
	              --blockSize=512 --length=$$L --output=/dev/null $$p ; } 2>&1 \
	            | awk '/^real/{print $$2}' ); \
	      [ -n "$$t" ] || { echo "cmaj render failed for $$p" >&2; exit 1; }; \
	      m=$$(python3 -c "print(min($$m,$$t))"); done; \
	    echo "$$p $$L $$m"; done; done | \
	  awk '{v[NR]=$$3} END {printf "Cmajor LLVM JIT: %.2f ns/frame\n", \
	        ((v[2]-v[1])-(v[4]-v[3]))/48e6*1e9}'

clean: ## remove every generated file
	rm -rf bench_all bench_cmaj bench_faust bench_jit bench_ocpp cpp v_scal.h v_vec.h v_48.h v_mcd0.h v_ocpp.h \
	       ir_cmaj.raw ir_faust.raw upstream upstream-p2 *.dSYM
