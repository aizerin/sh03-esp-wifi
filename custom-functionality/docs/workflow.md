# Building and testing

Run the following commands from this variant's directory:

```sh
make CROSS_COMPILE=arm-none-eabi-
python3 -m venv .venv-test
.venv-test/bin/pip install -r requirements-test.txt
make test-all CROSS_COMPILE=arm-none-eabi- PYTHON=.venv-test/bin/python
```

Omit `CROSS_COMPILE` to use the default PlatformIO toolchain path. `make` builds
`build/standalone/firmware.bin`, `.hex`, and `.elf` entirely from C sources,
configuration tables, and bundled FreeRTOS. Outputs and test reports are local
and ignored by Git.

`make test-standalone` executes the actual reset and FreeRTOS APIs in emulation
and performs an independent build without dumps, assembler, analysis exports,
or previous build outputs. The independent test also verifies a material-profile
change. These tests do not validate physical electrical behavior or safe heating.

In `custom-functionality`, `make ota` builds the initial factory image and OTA
package; `make test-link` checks UART and ESPHome, and `make test-ota` checks the
bootloader, update transactions, relocated application, display controls, and
uploader. See the root installation guide for wiring and first installation.

## Hardware testing

For hardware testing, follow the installation guide and use the ELF matching
the image you installed. The standalone image starts at `0x08000000`; installing
it over an OTA setup replaces the bootloader. For an OTA installation, use
`build/ota/factory.bin` initially and `build/ota/application.sh03` afterward.
