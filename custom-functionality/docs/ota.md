# Dryer firmware OTA through ESP32

The dryer MCU can be updated over Wi-Fi → encrypted ESPHome API → UART4.
ESPHome remains a single `esphome/sh03.yaml` file with lambdas. Updating the
ESP32 itself still uses standard ESPHome OTA; these are two separate firmwares.

The bootloader matches the detected `F1xx_HD`, chip ID `0x414`, 256 KiB flash,
2 KiB pages, and 64 KiB RAM. At runtime it checks the Cortex-M3 core and flash
size register. Any available nonzero ID must match `0x414`; a mismatch prevents
both writes and application startup. According to
[ST ES0340](https://www.st.com/resource/en/errata_sheet/es0340-stm32f101xcde-stm32f103xcde-device-errata-stmicroelectronics.pdf),
`DBGMCU_IDCODE` returns zero without a debugger, so reading zero must not block
standalone startup. The core and flash size alone do not identify the exact MCU;
confirm the `F1xx_HD / 0x414` profile through ST-Link during initial installation.
The exact chip marking, connector, and programming on the actual board have not
yet been verified. Verify the initial installation with an ST-Link available.
Builds and tests do not automatically flash the device.

## Wiring

| SH03 — MCU signal | ESP32-C3 Super Mini |
| --- | --- |
| PC10 / UART4 TX | GPIO21 / RX |
| PC11 / UART4 RX | GPIO20 / TX |
| GND | GND |

ESP32 pins can be changed in `substitutions`. **The SH03 connector signal positions
are unconfirmed.** Measure continuity on the powered-off board and verify 3.3 V
levels. **Communication and OTA require only RX/TX and a common ground.** The
ESP32 is not connected to SH03 reset, and the YAML reserves no reset GPIO.
During a normal upload, the application receives ENTER over UART and performs
a software reset into the bootloader. A programmer is used for initial installation
and any necessary repair.

The ESP32 is expected to be powered from the dryer board, so both processors
turn off together. Recovering a frozen application also does not require separate
ESP32 power; prepare the one-time recovery mode described below before cycling
power to both devices.

## Initial installation through ST-Link

Keep the original dump locally in the repository root. From the root, run:

```sh
cd custom-functionality
make ota
# Alternatively: make ota CROSS_COMPILE=arm-none-eabi-
```

| Output | Use |
| --- | --- |
| `build/ota/factory.bin` | Initial installation at `0x08000000`, exactly 256 KiB |
| `build/ota/application.sh03` | Every subsequent Wi-Fi update |
| `build/ota/bootloader.bin` / `.elf` | Bootloader for inspection/debugging; insufficient on its own |
| `build/ota-app/firmware.bin` / `.elf` / `.hex` | Application at `0x08002000`, intermediate output |
| `build/ota/manifest.json` | Sizes and checksums |

During initial verification, disconnect heater power and verify inactive control
outputs during reset, in the bootloader, and after installation. The following
command is an instruction; it was not run during development:

```sh
st-flash --reset write build/ota/factory.bin 0x08000000
```

The factory image includes the bootloader, application, and its valid manifest.
Without a request, the bootloader waits approximately **10 seconds** before
starting the application. This window lets an ESP32 powered from the same board
start; the bootloader does not wait for Wi-Fi during normal power-on.
Both fans are off from the start of bootloader initialization: GPIO holds their
PC6/PC7 control pins LOW, matching the original zero-duty PWM. They remain off
while receiving an update; the application then takes over through TIM8.
The display and beep start only after this wait. Also upload the current
`esphome/sh03.yaml` to the ESP32 and verify telemetry and HA control of both chambers.

If an older bootloader is installed with a three-second window, or a version
requiring nonzero `DBGMCU_IDCODE` even without a debugger, or a version leaving
fans at full speed while waiting, flash the current `factory.bin` again using
a programmer. `application.sh03` does not overwrite the bootloader. Update the
ESPHome YAML as well.

**After installing OTA, do not flash `build/standalone/firmware.bin` at the start
of flash** unless you intend to return to the non-OTA variant: it would overwrite
the bootloader. The default `make` still builds the original standalone variant
for ST-Link. Use `make ota` for all subsequent OTA changes.

## Subsequent Wi-Fi updates

Stop both chambers and exit panel editing/diagnostics. Firmware checks both
control task states, running flags, and zero heater output. If conditions are
not met, it returns `busy`; the uploader erases nothing.

From `custom-functionality`:

```sh
make ota
python3 -m venv .venv-ota
.venv-ota/bin/pip install -r tools/requirements-ota.txt
.venv-ota/bin/python tools/ota_upload.py build/ota/application.sh03 \
  --host filament-dryer-01.local --secrets esphome/secrets.yaml
```

An environment with ESPHome 2026.9.0 already includes the required libraries.
The hostname can be replaced with the ESP32 IP address. The uploader reads
`filament_dryer_01__api_key` from secrets, as the current YAML does; for older
local files it also supports `api_encryption_key` if the new name is absent.
Alternatively, use `SH03_API_KEY`, which takes precedence. It does not use the
ESP32 OTA password. Transfer uses the encrypted native API on port 6053 and
requires no internet, HTTP server, or image storage in ESP32 flash.

The uploader accepts only `.sh03` files with the correct address, layout, size,
vectors, and CRC. It rejects raw `.bin` files. It sends blocks of at most 60 bytes
and waits for the dryer's ACK; lost ACKs are handled by repeating the same block.
Success is reported only after application startup and confirmation of the
installed image's size and CRC. Drying does not resume automatically; RAM
settings return to defaults on restart.

During updates, the display does not redraw and normal SH03 telemetry/control
is unavailable. The ESP32 stays on Wi-Fi. `UART connected` may turn off after
five seconds; it returns after application startup.

## Interruption and recovery

- **Interrupted transfer:** the active image remains unchanged. After reset or
  30 seconds without requests, the bootloader starts the existing valid application.
  Run the upload again. Confirmed blocks can be resumed during the same power
  session; after reset, transfer starts from zero.
- **Power failure during installation:** staging and the committed pending manifest
  remain intact; after power-on, the bootloader copies and verifies the new image again.
- **Invalid application:** the bootloader waits for a transfer. It will not start
  an image with an invalid CRC.
- **Valid but faulty custom code that hangs:** run `--recover`, wait for preparation
  confirmation, then power-cycle SH03 and ESP32 together. Once Wi-Fi returns, the
  uploader installs the last working `.sh03`. A frozen application may not process
  the UART reset command. CRC does not detect logic errors, and automatic rollback
  to the previous version is not implemented.
- **Flash failure / geometry mismatch / damaged bootloader:** repair through ST-Link.
  This mechanism cannot repair faulty flash or persistently unstable power.

Recovery with an ESP32 sharing the power supply:

```sh
.venv-ota/bin/python tools/ota_upload.py build/ota/application.sh03 \
  --host filament-dryer-01.local --recover
```

1. Leave both devices on and run the command. The ESP32 must initially respond
   through its API, even if the dryer application has frozen.
2. The uploader stores a one-time recovery request in ESP32 NVS. It prompts for
   power-off only after synchronous flash write confirmation.
3. When prompted, power-cycle the dryer **including the ESP32**. Leave the uploader running.
4. On startup, the ESP32 reads and consumes the saved request. Before Wi-Fi
   initialization, it starts sending HELLO over UART every 250 ms. This keeps
   the dryer in the bootloader until the network returns.
5. The uploader reconnects, verifies an actual ESP32 restart using the boot ID,
   and takes over UART. Local HELLO messages stop before image transfer to avoid
   interfering with writes. Normal installation and verification then follow.

On ordinary power-on without prepared recovery, the ESP32 does not hold the
bootloader. The uploader waits up to four minutes for the shared restart and
Wi-Fi return. ESP32 local recovery mode has a five-minute limit; after expiration
it sends RUN and allows a valid application to start. Without a valid image,
the bootloader continues waiting for transfer. If the ESP32 is lost, a valid
application starts after 30 seconds without HELLO. Exiting the uploader attempts
to cancel the prepared request if the ESP32 is still reachable. After incomplete
recovery, `--recover` can be run again.

If the ESP32 does not start within the ten-second window or its firmware/API is
not working, repair through a programmer remains available. Do not run two
uploaders simultaneously. During normal transfer, control blocking is released
after 60 seconds if the client disappears; while awaiting recovery, the five-minute
limit applies.

## Flash and transactions

| Range (inclusive end) | Size | Contents |
| --- | ---: | --- |
| `08000000–08001FFF` | 8 KiB | Bootloader, never overwritten through OTA |
| `08002000–0801FFFF` | 120 KiB | Application |
| `08020000–0803DFFF` | 120 KiB | New image staging |
| `0803E000–0803E7FF` | 2 KiB | Pending manifest |
| `0803E800–0803EFFF` | 2 KiB | Active manifest |
| `0803F000–0803FFFF` | 4 KiB | Reserved |

Original tables remain at `0x08010B74`; the linker checks for overlap with code.
The slot is 120 KiB, but code space before the tables is smaller. If exhausted,
their absolute addresses will need refactoring. Application vectors are at
`0x08002000`; both the bootloader and application startup code set VTOR.

The manifest contains eight LE32 words: magic `0x55333053` (`S03U`), version `1`,
application address, byte count, image CRC32, layout `0x00010303`, CRC32 of the
first 24 bytes, and commit `0x5AA5A55A`. A `.sh03` contains this 32-byte header
and the image. CRC32 is IEEE/zlib, `123456789` → `CBF43926`. It is not a
cryptographic signature; the API key protects network access.

The bootloader verifies the entire staging CRC and vectors, then commits the
pending manifest as its **last write**. Installation first invalidates the active
manifest, then erases/rewrites the application slot and verifies it. Only then
does it write the new active manifest and erase pending. Staging remains untouched
throughout installation. The bootloader holds heater pins PB6/PB7 LOW.

The flash driver uses page erase and 16-bit writes according to
[ST PM0075](https://www.st.com/resource/en/programming_manual/pm0075-stm32f10xxx-flash-memory-microcontrollers-stmicroelectronics.pdf).
It does not change option bytes or perform a mass erase. Writes are restricted
to application/staging/metadata regions. The bootloader runs on the 8 MHz HSI;
UART4 uses divisor 69 (nominally 115942 baud, ESP32 at 115200 baud). Actual
HSI/UART tolerance must be verified on the specific chip.

## Interfaces and tests

Framing and CRC16 are the same as in the [regular protocol](uart-protocol.md).
Types below are hexadecimal:

| Type | Payload | Meaning |
| --- | --- | --- |
| `10` ENTER | empty | Application checks that drying has stopped and resets the MCU |
| `11` HELLO | empty | Bootloader: slot capacity in `size`, version in `crc` |
| `12` BEGIN | `base:u32,size:u32,crc:u32,layout:u32` | Prepare staging |
| `13` DATA | `offset:u32,data:2..60 B` | Even offset/length, sequential writes; identical duplicates allowed |
| `14` END | empty | Verify, commit, install, and start |
| `15` RUN | empty | Start the existing valid application |
| `17` INFO | empty | Application only: running image size and CRC |
| `18` REPLY | see below | Response with the request sequence number |

REPLY is 18 bytes:
`operation:u8,status:u8,next_offset:u32,size:u32,crc:u32,layout:u32`.
Status: 0 OK, 1 range, 2 busy, 3 CRC/vectors, 4 flash, 5 transfer state,
6 incorrect hardware. END ACK means the commit is stored; completion is
confirmed only by INFO after application startup.

API action `sh03_ota_packet` accepts `operation:int`, `sequence:int`, and
`payload:int[]`; the YAML constructs the frame. Text entity `SH03 OTA reply`
publishes `counter,sequence,operation,status,offset,size,crc,layout` in decimal.
The counter ensures delivery even for repeated ACKs.

API action `sh03_ota_recovery` takes `command:int`: 0 reads state, 1 prepares
the next shared restart, 2 cancels the request. It responds through the native
API (`supports_response: only`) with success status and JSON fields `boot_id`,
`holding`. `boot_id` is randomly regenerated on startup, so the uploader does
not mistake Wi-Fi disconnection for a restart. NVS key `0x53335230` holds the
one-time marker `0x53335231`; no drying commands are stored there.

```sh
make test-ota PYTHON=/path/to/python-with-test-dependencies
```

Tests require `requirements-test.txt`. They cover the actual C core with a flash
model, 5,184 failures before/during/after erases and writes, the maximum 120 KiB
image, invalid CRCs/vectors, the ARM bootloader and application startup, update
guards, 63 UART scenarios, uploader tests, and actual YAML lambdas under ASan/UBSan.
Recovery tests include shared restart, Wi-Fi reconnection, connection loss without
restart, NVS save failure, one-time request consumption, local HELLO without a
network, timeouts, and timer wraparound. ESPHome 2026.9.0 for ESP32-C3 was also
built into ESP-IDF firmware. Emulation does not confirm physical power, flash
timing, HSI, UART, reset, or power outputs.
