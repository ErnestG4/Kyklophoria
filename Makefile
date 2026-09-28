# kyklophoria — host build. The Alchemy Lab firmware build lands in
# shell/alchemy/Makefile at M1 (it expects ../alchemy-sdk like Audiothurgist).
#
#   make host    — build/host/kykdesk (desktop shell), kykspace (generate and
#                  grade spaces), kykeigen (bake a corpus into one) and
#                  kykworlds (grade every built-in world on one table) and
#                  alias_check (sweep any world at any cell for aliasing)
#   make test    — tests/run.sh (unit, aliasing, golden)
#   make tables  — regenerate core/kyk_tables.h
CXX      ?= g++
CORE_FLAGS = -std=gnu++17 -O2 -fno-exceptions -fno-rtti -ffp-contract=off -Wall -Wextra -Icore
# The desktop shell speaks HostLink with the SDK's header-only wire layer.
SDK_DIR   ?= ../alchemy-sdk
LINK_FLAGS = -Ishell/common -I$(SDK_DIR)/framework/include -DALCHEMY_HOSTLINK_MAX_BODY=1024
CORE_HDRS  = $(wildcard core/*.h) $(wildcard shell/common/*.h)

.PHONY: host test tables clean
host: build/host/kykdesk build/host/kykdesk-wavetable build/host/kykspace build/host/kykeigen build/host/kykworlds \
      build/host/alias_check

build/host/kykdesk: shell/desktop/main.cpp shell/desktop/wavio.h shell/desktop/script.h shell/desktop/serve.h $(CORE_HDRS)
	@mkdir -p build/host
	$(CXX) $(CORE_FLAGS) $(LINK_FLAGS) shell/desktop/main.cpp -o $@

# the desktop renderer built as the wavetable firmware builds the core: the
# resonator compiled out (KYK_RESONATOR=0). The suite renders every wavetable
# golden through it, so the firmware that ships is the core that is tested
build/host/kykdesk-wavetable: shell/desktop/main.cpp shell/desktop/wavio.h shell/desktop/script.h shell/desktop/serve.h $(CORE_HDRS)
	@mkdir -p build/host
	$(CXX) $(CORE_FLAGS) $(LINK_FLAGS) -DKYK_RESONATOR=0 shell/desktop/main.cpp -o $@

build/host/kykspace: tools/kykspace/main.cpp $(CORE_HDRS)
	@mkdir -p build/host
	$(CXX) $(CORE_FLAGS) tools/kykspace/main.cpp -o $@

# kykworlds grades every registered world on one table. Offline like the rest
# of tools/, so libm is fair game.
build/host/kykworlds: tools/kykworlds/main.cpp tools/corpus.h $(CORE_HDRS)
	@mkdir -p build/host
	$(CXX) -std=gnu++17 -O2 -Wall -Wextra -Icore tools/kykworlds/main.cpp -o $@

# kykeigen bakes a corpus into a space. Offline, so it may use libm and
# exceptions; the core flags stay off it apart from the include path.
build/host/kykeigen: tools/kykeigen/main.cpp shell/desktop/wavio.h $(CORE_HDRS)
	@mkdir -p build/host
	$(CXX) -std=gnu++17 -O2 -Wall -Wextra -Icore tools/kykeigen/main.cpp -o $@

# alias_check is a test, and also the instrument's measuring tool: `--world N
# --pos a b c d` sweeps any world at any cell, which is where the published
# aliasing figures come from. `make test` runs it with its defaults; this target
# is so the README's command is a command somebody can actually run.
build/host/alias_check: tests/alias_check.cpp $(CORE_HDRS)
	@mkdir -p build/host
	$(CXX) $(CORE_FLAGS) tests/alias_check.cpp -o $@

test: host
	tests/run.sh

tables:
	python3 tools/gen/gen_tables.py > core/kyk_tables.h

# armcost — instruction counts for the audio-path inner loops, compiled for
# the M7 the module actually runs.
#
# Desktop timings lie about this part by an order of magnitude. The wavefolder
# measured 2.7 us on x86 and was around 150 us on the module, because x86 hides
# a long serial VFP dependency chain and the M7 does not. Counting the
# instructions the target compiler emits catches that; a stopwatch on a laptop
# does not.
.PHONY: armcost
armcost:
	@mkdir -p build/arm
	@printf '#include "kyk_shapes.h"\nusing namespace kyk;\n'\
'extern "C" void c_fold(float* d, const float* s, int n, float k){ FoldFrame(d,s,n,k); }\n'\
'extern "C" void c_warp(float* d, const float* s, int n, float k){ WarpFrame(d,s,n,k); }\n'\
'extern "C" void c_ring(float* d, const float* s, int n, float k){ RingFrame(d,s,n,k); }\n'\
'extern "C" void c_crush(float* d, const float* s, int n, float k){ CrushFrame(d,s,n,k); }\n'\
'extern "C" void c_drop(float* d, const float* s, int n, float k){ DropFrame(d,s,n,k); }\n'\
'#include "kyk_resonate.h"\n'\
'extern "C" void c_resonate(ResonatorBank* b, float* d, int n){ b->Process(d,n); }\n'\
'extern "C" void c_pickup(Pickup* p, float* d, int n){ p->Process(d,n); }\n'\
'extern "C" void c_burst(BurstPlayer* b, float* d, int n){ b->Process(d,n); }\n'\
'extern "C" void c_wash(NoiseLayer* w, float* d, int n){ w->Process(d,n); }\n'\
'#include "kyk_exciter.h"\n'\
'extern "C" void c_bowed(ResonatorBank* b, float* d, int n, const float* w, Bow* x){ ProcessBowed(*b,d,n,w,*x,48000.f,0.01f); }\n'\
'extern "C" bool c_struck(ResonatorBank* b, float* d, int n, const float* w, Hammer* x){ return ProcessStruck(*b,d,n,w,*x,48000.f,0.01f); }\n'\
'extern "C" void c_blown(ResonatorBank* b, float* d, int n, const float* w, Reed* x){ ProcessBlown(*b,d,n,w,*x,50.f); }\n'\
'extern "C" void c_lipped(ResonatorBank* b, float* d, int n, const float* w, Lips* x){ ProcessLipped(*b,d,n,w,*x,50.f,48000.f); }\n'\
'extern "C" bool c_plucked(ResonatorBank* b, float* d, int n, const float* w, Pluck* x){ return ProcessPlucked(*b,d,n,w,*x,48000.f,0.01f); }\n'\
	  > build/arm/probe.cpp
	@arm-none-eabi-g++ -std=gnu++17 -O3 -mcpu=cortex-m7 -mfpu=fpv5-d16 -mfloat-abi=hard \
	  -mthumb -ffp-contract=off -Icore -c build/arm/probe.cpp -o build/arm/probe.o
	@echo "  Cortex-M7 instructions per shaper (whole function):"
	@for f in c_fold c_warp c_ring c_crush c_drop c_resonate c_pickup c_burst c_wash; do \
	  n=$$(arm-none-eabi-objdump -d build/arm/probe.o | awk -v fn=$$f '$$0 ~ "<"fn">:" {p=1;next} p && /^$$/ {exit} p' | grep -cE "^[[:space:]]+[0-9a-f]+:"); \
	  printf "    %-8s %3d\n" $$f $$n; done
	@echo "  the coupled exciters (docs/exciters.md): sample-outer, so the innermost loops are a mode's work a sample:"
	@python3 tools/armloops.py build/arm/probe.o 'kyk::ResonatorBank::Process(' c_bowed c_struck c_blown c_lipped c_plucked

clean:
	rm -rf build/host
