# proto/ — the synthesised exciter and the waveguide, desktop only.
#
#   make -C proto          the library the fitter and renderer load
#   make -C proto armcost  M7 instruction counts for the new inner loops
#
# Builds into ../build/proto (git-ignored). Includes ../runtime/kyk_resonate.h,
# the verbatim copy of Kyklophoria's core that `make check-runtime` keeps.
CXX   ?= g++
OUT    = ../build/proto
FLAGS  = -std=gnu++17 -O2 -Wall -Wno-misleading-indentation -fPIC

.PHONY: all armcost clean
all: $(OUT)/libkykproto.so

$(OUT)/libkykproto.so: proto_api.cpp kyk_exciter.h ../runtime/kyk_resonate.h
	@mkdir -p $(OUT)
	$(CXX) $(FLAGS) -shared proto_api.cpp -o $@

# the inner loops as the module's compiler emits them: the noise bands and the
# waveguide a sample, the bank a mode a sample for scale
armcost: armcost.cpp kyk_exciter.h
	@mkdir -p $(OUT)
	arm-none-eabi-g++ -std=gnu++17 -O3 -mcpu=cortex-m7 -mfpu=fpv5-d16 -mfloat-abi=hard \
	  -mthumb -ffp-contract=off -fno-exceptions -fno-rtti -c armcost.cpp -o $(OUT)/armcost.o
	arm-none-eabi-objdump -d --no-show-raw-insn $(OUT)/armcost.o > $(OUT)/armcost.s
	@python3 armcount.py $(OUT)/armcost.s

clean:
	rm -rf $(OUT)
