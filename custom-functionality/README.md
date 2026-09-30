# SH03 — custom firmware + ESPHome / Home Assistant

This directory started as a copy of `reconstruction`.
The original reconstruction remains unchanged in the adjacent directory.
Further modifications belong here. The default build adds bidirectional UART4
communication for an ESP32-C3 Super Mini running ESPHome to the original control logic.

The dryer can also be updated **over Wi-Fi through the ESP32**: `make ota` creates
a bootloader, an initial installation image, and a package for subsequent wireless
updates. See the [OTA guide](docs/ota.md). Initial installation requires ST-Link.

## Features

For each of the two chambers, Home Assistant provides:

- the temperature used by the display, humidity, heater temperature, fan speed,
  requested heater output, and remaining time;
- program enabled state, actual drying state, and fault text;
- a start/stop switch, target temperature of 45–85 °C, duration of 2–98 hours in
  two-hour increments, Timed/Humidity mode, humidity threshold of 20–40% in
  5% increments, and material selection.

**Change settings while the chamber is stopped.** During operation or local panel
editing, the firmware returns `Busy`. Then turn on the drying switch. Humidity
mode preserves the original logic and two-hour follow-on period; duration changes
are rejected in this mode. `Drying enabled` may be on while the chamber waits
for humidity to exceed the threshold and `Actively drying` is off.

Commands receive acknowledgments, but entities update only from the dryer's
actual state. `Last command` shows acceptance, rejection, or timeout. Commands
are not automatically repeated after a timeout or restart. Loss of Wi-Fi,
Home Assistant, or ESP32 does not interrupt an already running drying program.
After five seconds without data, `UART connected` turns off; measurements become
unknown, controls retain their last confirmed state, and new commands are rejected.

Both displays share the **Displays enabled** switch. OFF completely turns off
the segments and backlight of both panels; ON lights them with current data.
Automatic turn-off has been removed. The first touch only wakes the displays;
the next operates the dryer normally. Once awake, they stay on until manually
switched off. Drying continues with the displays off. Displays are on after a
restart. PB1 was confirmed as the shared backlight control on the actual board.
The new [esphome/sh03.yaml](esphome/sh03.yaml) removes the **Display auto-off**
number entity; the existing switch also works with older ESPHome configurations.
See the [UART protocol](docs/uart-protocol.md) for details.

## Wiring

The original firmware already initializes **UART4, PC10 TX / PC11 RX, 115200 8N1**.
In this custom variant, UART is reserved for the protocol and original text debug
output is suppressed. ESPHome uses GPIO21 RX / GPIO20 TX, matching the working
`filament-dryer-01` wiring. Pins can be changed in the substitutions section of
[esphome/sh03.yaml](esphome/sh03.yaml).

| Dryer — MCU pin | ESP32-C3 Super Mini |
| --- | --- |
| PC10 / UART4 TX | GPIO21 / RX |
| PC11 / UART4 RX | GPIO20 / TX |
| GND | GND |

Control and OTA require only TX/RX and a common ground. A UART command and SH03
software reset enter the bootloader; no extra GPIO is needed. The ESP32 can be
powered from the dryer board. Recovery with `--recover` first stores a one-time
request in the ESP32; both devices can then be power-cycled together. After
startup, the ESP32 holds the bootloader over UART until Wi-Fi returns.
See the [OTA recovery procedure](docs/ota.md#interruption-and-recovery).

**The location of these signals on the board connector has not yet been confirmed.**
The dump identifies MCU pins, not connector pin order. Before connecting, identify
ground and measure TX/RX continuity to the MCU with the board disconnected.
Verify 3.3 V logic levels; do not connect ESP32 GPIO to 5 V. Arrange ESP32 power
separately using a verified supply; do not assume the voltage on an unlabeled connector.

## Dryer firmware

From the repository root:

```sh
cd custom-functionality
make CROSS_COMPILE=arm-none-eabi-
```

With the default local PlatformIO toolchain, simply run `make`. Outputs are
`build/standalone/firmware.bin`, `.hex`, and `.elf`. The BIN is built for address
`0x08000000`. Builds and tests do not flash the device.

For ESPHome, use **standalone** (without OTA) or `make ota` with the
[bootloader](docs/ota.md).

## ESPHome

**ESPHome 2026.9.0**, ESP-IDF, and the ESP32C3 variant were used and verified.
The entire integration is in one [esphome/sh03.yaml](esphome/sh03.yaml): standard
`template` entities, `uart`, `script`, and `interval` with lambdas. No
`external_components`, custom headers, or other source files are required.
Only the YAML and your credentials in `secrets.yaml` are needed.

For the existing **filament-dryer-01**, always copy the **entire current
[sh03.yaml](esphome/sh03.yaml)** into the device configuration in ESPHome Device
Builder and select Install. The file already includes its name, RX=GPIO21 /
TX=GPIO20 wiring, both Wi-Fi networks, `power_save_mode: none`, `8.5dB` transmit
power, and the latest controls including displays. Keep the existing ESPHome
`secrets.yaml` unchanged; it uses the existing names `wifi_ssid`, `wifi_ssid_2`,
`wifi_password`, `filament_dryer_01__api_key` (two underscores before `api`),
and `ota_password`. Subsequent updates do not require re-entering pins or Wi-Fi
settings, or merging individual lambdas. `secrets.example.yaml` is only a template
for a new installation; it does not replace your existing secrets.

For a new local installation:

```sh
# In custom-functionality:
python3 -m venv .venv
.venv/bin/pip install -r esphome/requirements.txt
cp esphome/secrets.example.yaml esphome/secrets.yaml
# Fill in Wi-Fi, a new API encryption key, and the OTA password in secrets.yaml.
.venv/bin/esphome config esphome/sh03.yaml
.venv/bin/esphome run esphome/sh03.yaml
```

Perform the first upload over USB; `esphome run` offers device selection.
The ESP32 and dryer each need their own firmware. In Home Assistant, add the
ESPHome integration for `filament-dryer-01` and enter the same API encryption key.
With ESPHome Device Builder, copy only the YAML and add the required values to
`secrets.yaml` in its configuration directory. Passwords and build outputs are
excluded by `.gitignore`.

USB logs use `logger.hardware_uart: USB_SERIAL_JTAG` and `logger.baud_rate: 115200`.
They are available without Wi-Fi; GPIO20/21 are reserved for dryer communication.
`logger.baud_rate: 0` also disables USB logs. For startup or connection problems,
run `esphome logs esphome/sh03.yaml --device /dev/cu.usbmodem...` with the ESP32
port, or open USB logs in ESPHome Web. Once logs are open, press RESET and
observe the entire startup, including Wi-Fi connection.

## Changes and tests

- [src/link.c](src/link.c): IRQ, RX/TX buffers, frames, CRC, telemetry, and acknowledgments.
- [src/remote.c](src/remote.c): command validation and delivery to control tasks.
- [include/sh03_protocol.h](include/sh03_protocol.h): protocol format and parser.
- [esphome/sh03.yaml](esphome/sh03.yaml): HA entities and UART communication in lambdas.
- [docs/uart-protocol.md](docs/uart-protocol.md): exact interface specification.

The lambda protocol matches the firmware header; tests compare actual transmitted
and received bytes. Update both sides when changing the protocol. If profile names
in `config/materials.json` change, also update the material lists in both YAML
`select` entities; a test checks that they match.

```sh
python3 -m venv /tmp/sh03-test-venv
/tmp/sh03-test-venv/bin/pip install -r requirements-test.txt
make test-all PYTHON=/tmp/sh03-test-venv/bin/python
# Only the new interface and ESPHome lambdas:
make test-link PYTHON=/tmp/sh03-test-venv/bin/python
# Bootloader, updates, relocated application, and uploader:
make test-ota PYTHON=/tmp/sh03-test-venv/bin/python
```

New interface tests execute the actual compiled ARM IRQ handler, parser, and
commands with RTOS queues. The host test loads lambdas directly from YAML and
runs their C++ code with substituted UART and time, including AddressSanitizer/UBSan.
It requires a host C++17 compiler (`CXX`, default `c++`). The complete
ESPHome firmware is verified through an actual ESP32-C3 build. Reports are
generated locally in `analysis/standalone`.

**Not yet validated on a physical board.** Emulation does not cover full task
switching or electrical responses. Initial checks should verify TX/RX, telemetry,
both chambers, the panel, start/stop, rejection during faults, and UART disconnection.
Keep your original dump for restoration.

Original control architecture: [docs/standalone.md](docs/standalone.md),
[docs/hardware.md](docs/hardware.md), [docs/limitations.md](docs/limitations.md).
The standard features used are described in the official documentation for
[ESPHome UART](https://esphome.io/components/uart/),
[template switches](https://esphome.io/components/switch/template/),
and [switch state restoration](https://esphome.io/components/switch/).
