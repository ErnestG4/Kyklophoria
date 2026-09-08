# kyklophoria — host build. The Alchemy Lab firmware build lands in
# shell/alchemy/Makefile at M1 (it expects ../alchemy-sdk like Audiothurgist).
#
#   make host    — build/host/kykdesk (desktop shell) and build/host/kykspace
#   make test    — tests/run.sh (unit, aliasing, golden)
#   make tables  — regenerate core/kyk_tables.h
CXX      ?= g++
CORE_FLAGS = -std=gnu++17 -O2 -fno-exceptions -fno-rtti -ffp-contract=off -Wall -Wextra -Icore
# The desktop shell speaks HostLink with the SDK's header-only wire layer.
SDK_DIR   ?= ../alchemy-sdk
LINK_FLAGS = -Ishell/common -I$(SDK_DIR)/framework/include -DALCHEMY_HOSTLINK_MAX_BODY=1024
CORE_HDRS  = $(wildcard core/*.h) $(wildcard shell/common/*.h)

.PHONY: host test tables clean
host: build/host/kykdesk build/host/kykspace

build/host/kykdesk: shell/desktop/main.cpp shell/desktop/wavio.h shell/desktop/script.h shell/desktop/serve.h $(CORE_HDRS)
	@mkdir -p build/host
	$(CXX) $(CORE_FLAGS) $(LINK_FLAGS) shell/desktop/main.cpp -o $@

build/host/kykspace: tools/kykspace/main.cpp $(CORE_HDRS)
	@mkdir -p build/host
	$(CXX) $(CORE_FLAGS) tools/kykspace/main.cpp -o $@

test: host
	tests/run.sh

tables:
	python3 tools/gen/gen_tables.py > core/kyk_tables.h

clean:
	rm -rf build/host
