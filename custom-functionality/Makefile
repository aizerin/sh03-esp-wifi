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

test-all: test-standalone test-link test-ota

.PHONY: ota test-ota test-link
ota:
	$(PYTHON) tools/build_ota.py

test-link: standalone
	$(PYTHON) tools/test_link.py
	$(PYTHON) tools/test_esphome.py

test-ota: ota
	$(PYTHON) tools/test_update.py
	$(PYTHON) tools/test_bootloader.py
	$(PYTHON) tools/test_ota_app.py
	$(PYTHON) tools/test_ui_rtos.py
	$(PYTHON) tools/test_remote_display.py
	$(PYTHON) tools/test_display_power.py
	$(PYTHON) tools/test_ota_upload.py
	$(PYTHON) tools/test_esphome.py
