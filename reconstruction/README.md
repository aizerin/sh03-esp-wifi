# SH03 V3.5.1 — reconstructed firmware

This project contains a **standalone buildable reconstruction of the entire firmware**:
the application, display, sensors, board configuration, drivers, reset, interrupt
vectors, and FreeRTOS. The default build uses C sources and bundled data tables.
It does not require the original dump, Ghidra, or `firmware.S`.

This is a newly built implementation of the inferred behavior. The manufacturer's
original names, comments, and project cannot be recovered from a binary dump.
The NTC bugs and indeterminate initialization values found during analysis are
fixed and described below. The new image has not yet been tested on a physical
board or flashed to a device.

## Building

```sh
cd reconstruction
make
```

Outputs for further development and hardware testing:

- **[build/standalone/firmware.bin](build/standalone/firmware.bin)** — complete new image starting at `0x08000000`.
- [build/standalone/firmware.hex](build/standalone/firmware.hex) — the same image in Intel HEX format.
- [build/standalone/firmware.elf](build/standalone/firmware.elf) — image with debugger symbols.
- [build/standalone/manifest.json](build/standalone/manifest.json) — hash, sources, and mappings from original addresses to new symbols.

The build requires Python 3 and ARM GCC. The default GCC path matches a local
PlatformIO installation; override it with `make CROSS_COMPILE=arm-none-eabi-`.
FreeRTOS 11.1.0 is bundled in `vendor/FreeRTOS-Kernel`, including its license
and upstream commit identification. Nothing is downloaded during the build.

## Where to make changes

| File | Contents |
| --- | --- |
| [src/control.c](src/control.c) | Two PID loops and their parameters, temperature band, CRC |
| [src/drying.c](src/drying.c) | Drying state machine, commands, and countdown |
| [src/ui.c](src/ui.c), [src/keys.c](src/keys.c) | Settings, materials, short/long presses, and panel timers |
| [src/sensors.c](src/sensors.c), [src/acquisition.c](src/acquisition.c), [src/ntc.c](src/ntc.c) | GXHT30, NTC, Hall, fan speed, and filtering |
| [src/monitor.c](src/monitor.c), [src/outputs.c](src/outputs.c) | Faults, heating, fans, actuators, and alarm |
| [src/display.c](src/display.c), [src/display_bus.c](src/display_bus.c) | Segments, fonts, icons, cache, and display transfers |
| [src/board.c](src/board.c) | Board peripheral configuration and connections |
| [src/gpio.c](src/gpio.c), [src/adc.c](src/adc.c), [src/dma.c](src/dma.c), [src/i2c.c](src/i2c.c) | Drivers and interrupt handling |
| [src/timers.c](src/timers.c), [src/uart.c](src/uart.c), [src/rcc.c](src/rcc.c) | PWM, capture, UART, and clocks |
| [src/reset.c](src/reset.c), [src/startup.c](src/startup.c) | Reset, vectors, RAM, main, and task creation |
| [include/FreeRTOSConfig.h](include/FreeRTOSConfig.h) | Recovered RTOS configuration |
| [config/materials.json](config/materials.json) | Ten profiles: name, temperature, duration in seconds |
| [config/flash-tables.json](config/flash-tables.json) | NTC table, fonts, segments, and hardware descriptors |
| [config/initial-state.json](config/initial-state.json) | Initial application RAM, including hardware descriptors |

After editing C sources or configuration, simply run `make` again.
`control.c` is only a small part of the entire program.

## Verification

```sh
python3 -m venv /tmp/sh03-test-venv
/tmp/sh03-test-venv/bin/python -m pip install -r requirements-test.txt
make test-all PYTHON=/tmp/sh03-test-venv/bin/python
```

The standalone image passed 32,429 differential cases against the original ARM
instructions and 65,554 checks of the corrected NTC conversion. Task comparisons
cover 13,664 steps. Tests also execute the actual reset, board initialization,
scheduler preparation, and integration tests of real FreeRTOS APIs. An independent
test builds the project without the dump, analysis, previous outputs, or any `.S`
files, and also checks a material profile change.

Available source-only test results, including the tested image hash, are written to `analysis/standalone`.
Builds and test reports are not tracked. Emulation uses controlled peripheral
responses; it does not simulate the board's electrical properties or complete
task switching and interrupt delivery.

Intentional fixes relative to the original:

- NTC search initializes its midpoint and can return 0 °C without looping forever.
- Fan timer configuration and disabled I²C DMA no longer inherit uninitialized stack values.

Details: [limitations and behavior changes](docs/limitations.md),
[standalone build architecture](docs/standalone.md),
[reconstruction status](docs/reconstruction-status.md),
[hardware map](docs/hardware.md).

## Current test suite

The verification figures above describe the original reconstruction work.
`make test-all` runs reset/FreeRTOS checks and an independent source-only build
with a material-profile edit. See [workflow.md](docs/workflow.md) for commands.
