# Hardware and memory map

MCU identification: the debugger reported ID `0x414`, 256 KiB flash, and 64 KiB RAM.
The code uses the STM32F1 memory map and Cortex-M3 instructions. The exact chip
marking and physical left/right chamber assignment have not yet been verified
on the board.

## Pins inferred from initialization tables

Chamber 0/1 is a software index; physical assignment must be confirmed.

| Signal | Chamber 0 | Chamber 1 | Evidence |
| --- | --- | --- | --- |
| Heater PWM | PB7 / TIM4 CH2 | PB6 / TIM4 CH1 | Descriptors `0x200001E0`, init `0x080049F8` |
| Fan PWM | PC6 / TIM8 CH1 | PC7 / TIM8 CH2 | Descriptors `0x20000034`, init `0x080034A4` |
| Fan feedback | PA8 / TIM1 CH1 | PA9 / TIM1 CH2 | Same descriptors, input capture and DMA |
| NTC | PA4 / ADC1 CH4 | PA5 / ADC1 CH5 | Descriptors `0x20000210`, init `0x08005290` |
| Hall ADC, first channel | PC3 / ADC3 CH13 | PC0 / ADC3 CH10 | Descriptors `0x20000150` |
| Hall ADC, second channel | PC2 / ADC3 CH12 | PC1 / ADC3 CH11 | Descriptors `0x20000150` |
| Actuator 0, A/B pulses | PB8 / PB9 | PB13 / PB14 | Descriptors `0x200000F0`, 65-tick pulses |
| Actuator 1, A/B pulses | PA11 / PA12 | PB15 / PC9 | Same descriptors |

The physical actuator type cannot be established from GPIO instructions alone.
The code pulses two outputs per actuator and checks for a Hall ADC change during
diagnostics. No claim is made yet about whether this is a motor, damper, or another design.

Other observed signals:

- Debug UART: UART4, PC10 TX and PC11 RX, 115200 baud (`0x0800B17C`).
- Touch input: PA3/EXTI3; the code mentions XW05A (`0x0800B358`).
- Sensor communication uses I2C2 (`0x40005800`); see control.md for GXHT30 addresses.
- The alarm writes to bit-band address `0x422181B0`, corresponding to GPIOB ODR bit 12.
- The display uses two sets of tables and bitwise transfers; the controller's
  identity has not been definitively established by the analysis so far.

The custom switch for both displays uses commands compatible with the
[Holtek HT1621 Command Summary](https://www.holtek.com/webapi/116711/HT1621_21Gv340.pdf).
The original transfer sends prefix `1000` followed by eight argument bits:
its initialization value `0x06` therefore corresponds to `LCD ON` (`100 00000011 0`);
switching off uses `0x04` (`100 00000010 0`, `LCD OFF`). RAM and the oscillator
remain active, and normal rendering continues. Compatibility is inferred from
the format and original initialization, not a chip marking. On the actual board,
LCD OFF alone blanks only the segments; full power-off of the display illumination
also requires PB1 control.

### Backlight: confirmed shared PB1 output

On **2026-09-30**, the user confirmed on the actual dryer that driving **PB1 LOW**
completely turns off both panels, including the backlight. **HIGH turns the
backlight on**. A diagnostic OTA application (CRC32 `747af6d7`) drove PB1 LOW
for three seconds after the startup animation, then restored its original state.
Normal firmware no longer includes the diagnostic pulse or repeated startup
animation.

The PB1 descriptor at `0x20000290` contains GPIOB `0x40010C00`, mask `0x0002`,
and pin number 1. Original function `0x08001980`
([sh03_aux_output_init](../src/board.c)) configures it as a push-pull output LOW.
Setter `0x08002580` uses an inverted argument: **0 writes HIGH, 1 writes LOW**
through bit-band alias `0x42218184`. It changes only PB1, not other GPIOB outputs
such as heater pins PB6/PB7 or buzzer PB12.

The original `main` turns this output on after LCD initialization and clearing,
before the startup animation. That order is preserved. The bootloader does not
configure PB1; the panels remain dark during its ten-second waiting window.

At runtime, OFF first turns off PB1, then disables LCD outputs. ON first restores
LCD outputs with current RAM contents, then turns on the backlight. Automatic
turn-off has been removed; the first touch only wakes both panels. Neither the
switch nor waking the displays changes drying control.

`tools/test_display_power.py` executes actual ARM command code and PB1 writes:
it checks LCD/backlight order, preservation of other GPIO/PWM outputs, current
LCD RAM contents, telemetry, wake-up, and continuous illumination without a timer.
`tools/test_ota_app.py` verifies normal startup with PB1 LOW → HIGH, no test pulse,
and zero heater/fan duty.

## Original and intermediate image memory

| Range / address | Purpose |
| --- | --- |
| `0x08000000–0x0800012F` | Vector table |
| `0x08000130–0x08010A77` | Code, runtime, and embedded constants/tables |
| `0x08010BAC–0x08010BFB` | Material profiles |
| `0x08010BFC` | NTC table, 220 uint16 entries for −20 through 199 |
| `0x08010F09` | CRC table, 256 bytes |
| `0x08011074` onward | Log strings and task names |
| `0x08011500` | Two scatter-load records: data and BSS |
| `0x08011650–0x0801174F` | Compressed RAM initializer, 256 bytes |
| `0x08011750–0x0803FFFF` | All `0xFF` in the original dump |
| `0x08018000` | Start of the optional new C extension |
| `0x20000000–0x200002A7` | Initialized data; 680 bytes after decompression |
| `0x200002A8–0x20005EFF` | Region zeroed by startup |
| `0x20000344`, `0x20000354` | Messages for the two drying tasks |
| `0x200003B4`, `0x200003C8` | Two sensor snapshots |
| `0x200004FC`, `0x200005CC` | Two control structures, 208 bytes each |
| `0x2000069C`, `0x200006A0` | Countdown counters |
| `0x200006D4`, `0x200006D6` | Latest raw NTC ADC samples |
| `0x20005F00` | Initial SP |

The initializer is decompressed using an algorithm derived from `0x0800016C`,
not taken from the running SRAM snapshot. Reproduce it with `tools/extract_initial_data.py`.
The entire RAM is not treated as the original ELF `.data`: the final part remains
uninitialized, the first 680 bytes are RW, and peripheral blocks are marked
volatile in Ghidra.

The register map matches [ST RM0008](https://www.st.com/resource/en/reference_manual/cd00171190.pdf)
and [CMSIS STM32F103xE](https://github.com/STMicroelectronics/cmsis-device-f1/blob/master/Include/stm32f103xe.h).

## Standalone firmware

Application descriptors and state in the first 0x700 bytes of RAM retain their
original offsets. The new kernel and BSS are linked from 0x20000700, and the
main SP is 0x20010000. Code has new addresses and symbols in the standalone ELF;
tables remain at their original addresses. See [standalone.md](standalone.md).
