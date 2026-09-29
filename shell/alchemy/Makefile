# =============================================================================
# kyklophoria — Hermetic Modular Alchemy Lab v2 firmware (shell/alchemy).
# Same layout as Audiothurgist: sibling clones in the grandparent directory.
#   ../../../alchemy-sdk   (github.com/hermetic-modular/alchemy-sdk, v0.11 + local boost patch)
#
#   make libdaisy      — build the SDK's vendored libDaisy once after cloning
#   make               — build/kyklophoria.bin (BOARD=v2)
#   make program-dfu   — flash (module parked in the Daisy bootloader)
#   make program-live  — reboot the running module over USB and flash it
# =============================================================================
# Two firmwares from one tree (Combust, 28 September: "let's swap to be two
# firmwares ... users swap from the SD"): each with the whole RAM, the pots and
# the jacks to itself.
#   make MODE=wavetable   -> build-wavetable/kyklophoria-wavetable.bin
#   make MODE=modal       -> build-modal/bongs.bin (the modal firmware is Bongs)
#   make both             -> the two
MODE ?= modal
ifeq ($(filter $(MODE),wavetable modal),)
$(error MODE is wavetable or modal, not "$(MODE)")
endif
ifeq ($(MODE),modal)
TARGET = bongs
else
TARGET = kyklophoria-$(MODE)
endif
# the SDK's Makefile sets BUILD_DIR plainly; a mode's objects are its own
override BUILD_DIR := build-$(MODE)
BOARD ?= v2

ALCHEMY_DIR  = ../../../alchemy-sdk
LIBDAISY_DIR = $(ALCHEMY_DIR)/vendor/libDaisy

CPP_SOURCES = main.cpp
CPP_SOURCES += $(sort $(shell find $(ALCHEMY_DIR)/framework/src -name '*.cpp'))
CPP_SOURCES += $(sort $(wildcard $(ALCHEMY_DIR)/hardware/alchemy-lab/$(BOARD)/src/*.cpp))

C_INCLUDES += \
    -I. \
    -I../../core \
    -I../common \
    -I$(ALCHEMY_DIR)/framework/include \
    -I$(ALCHEMY_DIR)/hardware/include \
    -I$(ALCHEMY_DIR)/hardware/alchemy-lab/$(BOARD)/include \
    -I$(LIBDAISY_DIR)/Middlewares/Third_Party/FatFs/src

USE_FATFS = 1

ifeq ($(BOARD),v2)
C_DEFS += -DALCHEMY_BOARD_V2
C_DEFS += -DALCHEMY_LOCK_INLINE_RAM_LIMIT=0
endif
# Telemetry with spectrum + frame is ~460 bytes: 1024-byte bodies, one request per frame.
C_DEFS += -DALCHEMY_HOSTLINK_MAX_BODY=1024
# Stamp the build so the module can say which one it is. Without this every
# flash reported the same string and there was no way to tell from the panel
# or the web page whether the binary in front of you was the new one.
GIT_HASH := $(shell git rev-parse --short HEAD 2>/dev/null || echo unknown)
GIT_DIRTY := $(shell git diff --quiet 2>/dev/null || echo -dirty)
C_DEFS += -DKYK_GIT_HASH=\"$(GIT_HASH)$(GIT_DIRTY)\"
# Core determinism: no FMA contraction (docs/spec.md §2).
C_DEFS += -ffp-contract=off
ifeq ($(MODE),wavetable)
C_DEFS += -DKYK_MODE_WAVETABLE=1 -DKYK_RESONATOR=0
else
C_DEFS += -DKYK_MODE_MODAL=1 -DKYK_RESONATOR=1 -DKYK_WAVETABLE=0
endif
# extra defines for a variant build, e.g. make KYK_DEFS=-DKYK_RES_NEAREST
C_DEFS += $(KYK_DEFS)

APP_TYPE = BOOT_SRAM
LDSCRIPT = kyklophoria.lds
CPP_STANDARD = -std=gnu++17

SYSTEM_FILES_DIR = $(LIBDAISY_DIR)/core
include $(SYSTEM_FILES_DIR)/Makefile

.PHONY: both
both:
	$(MAKE) MODE=wavetable
	$(MAKE) MODE=modal

.PHONY: libdaisy
libdaisy:
	$(MAKE) -C $(LIBDAISY_DIR)

USBPID ?= df11
.PHONY: program-live
program-live: all
	node $(ALCHEMY_DIR)/tools/hostlink-cli/hostlink.mjs reboot bootloader
	dfu-util -w -a 0 -s $(FLASH_ADDRESS):leave -D $(BUILD_DIR)/$(TARGET_BIN) -d ,0483:$(USBPID)
