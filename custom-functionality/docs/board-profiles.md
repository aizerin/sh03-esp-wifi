# Explicit MCU profiles

The default build remains `m3-64k`. A second SH03 unit was measured on
2026-10-19 with CPUID `0x410fc241` (Cortex-M4), debug ID `0x21040414`, and
a flash-size register of 256 KiB. ST-Link 1.8.0 nevertheless describes this
unit as `F1xx_HD` with 64 KiB SRAM. That lookup is not a measurement of SRAM.
The exact manufacturer and part number have not been confirmed.

On this unit, SWD writes at `0x2000bff0` succeed, while writes at
`0x2000fff0` fail. The old initial stack at `0x20010000` causes a stacking
bus fault before GPIO initialization. Running the same bootloader with its
stack moved below `0x2000c000` reaches GPIO initialization, driving fans and
heaters low, then correctly rejects the M4 core under the old M3-only guard.
Its original firmware dump is byte-identical to the repository's original
flash dump and uses a much lower initial stack (`0x20005f00`).

Build explicitly for the measured M4 unit:

```sh
SH03_BOARD=m4-48k make -C custom-functionality ota
SH03_BOARD=m4-48k make -C custom-functionality test-ota PYTHON=/path/to/test/python
```

Both application and bootloader then use a 48 KiB RAM ceiling and stack top
`0x2000c000`. The core check requires Cortex-M4, retains the 256 KiB flash
and compatible debug-ID checks, and OTA rejects vectors with a stack above
that ceiling. The default M3 profile still rejects M4. Code remains Thumb-2
soft-float, using the existing FreeRTOS CM3 port without FPU use.

Outputs still use `build/ota` and `build/ota-app`; manifests record the board
profile and RAM ceiling. Always retain each unit's backup and installed image.
Use the same profile for subsequent application OTA builds. Do not install
the default 64 KiB factory image on this M4 unit. This profile does not prove
compatibility of any other M4 microcontroller or board revision.

The bootloader test maps only the selected RAM size. The startup integration
test also removes RAM above that size until the actual application reaches
the scheduler. Transaction tests reject a correctly checksummed image whose
initial stack exceeds the selected RAM ceiling. These checks do not replace
physical startup, control-output and UI verification.
