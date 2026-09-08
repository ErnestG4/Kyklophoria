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
TARGET = kyklophoria
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
# Core determinism: no FMA contraction (docs/spec.md §2).
C_DEFS += -ffp-contract=off

APP_TYPE = BOOT_SRAM
LDSCRIPT = kyklophoria.lds
CPP_STANDARD = -std=gnu++17

SYSTEM_FILES_DIR = $(LIBDAISY_DIR)/core
include $(SYSTEM_FILES_DIR)/Makefile

.PHONY: libdaisy
libdaisy:
	$(MAKE) -C $(LIBDAISY_DIR)

USBPID ?= df11
.PHONY: program-live
program-live: all
	node $(ALCHEMY_DIR)/tools/hostlink-cli/hostlink.mjs reboot bootloader
	dfu-util -w -a 0 -s $(FLASH_ADDRESS):leave -D $(BUILD_DIR)/$(TARGET_BIN) -d ,0483:$(USBPID)
