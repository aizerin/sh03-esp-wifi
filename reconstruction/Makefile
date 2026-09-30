# Override CROSS_COMPILE for another ARM GNU toolchain installation.
CROSS_COMPILE ?= $(HOME)/.platformio/packages/toolchain-gccarmnoneeabi/bin/arm-none-eabi-
export CROSS_COMPILE
PYTHON ?= python3

.PHONY: all standalone test test-standalone test-all
all: standalone

standalone:
	$(PYTHON) tools/build_standalone.py

test: test-all

test-standalone: standalone
	$(PYTHON) tools/test_system.py
	$(PYTHON) tools/test_build.py

test-all: test-standalone
