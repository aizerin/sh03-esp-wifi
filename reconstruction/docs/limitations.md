# What has been verified and what remains to be checked

> Historical analysis record. For the current test suite and available commands,
> see [workflow.md](workflow.md).

## Reconstruction completeness

Byte-level completeness is verified: the sources build all 256 KiB, including
vectors, runtime, data, and the empty tail. With default profiles, the full
SHA-256 matches the original dump. This does not automatically establish that
all functions and types have been named or every branch explained.

The Ghidra pseudocode contains 375 identified functions. Some ARM runtime
functions share blocks, have internal entry points, or use table-driven exception
handling; function boundaries may differ from the original source project.
Summing function body sizes is therefore unsuitable for calculating byte coverage.
Unique recognized instructions cover 67,016 bytes; another 4,488 bytes within
the occupied image are preserved data or unclassified bytes.

Warnings are listed in `analysis/decompiler-warnings.json`. Read startup, printf,
and soft-float runtime pseudocode especially carefully: Ghidra reports indirect
jumps or embedded data after calls in these sections. Verified assembly remains
authoritative for them. The export has not been presented as the original C.

## Identified bug in the original NTC conversion

At `0x08005140`, `ntc_adc_to_temperature` (`0x0800512C`) reads the byte at
`[sp + 12]` before initialization and shifts it right. This byte becomes the
first midpoint of the table's binary search. The inner loop also treats a
resulting temperature of 0 as the condition for having no result yet.

Thirty-two emulation cases used different ADC values and initial values of
that stack byte. For ADC **3961**, approximately 0 °C according to the firmware's
own table, the function failed to return within 20,000 instructions for three
of the four tested stack values. Results are in `analysis/ntc-audit.json`.
This is a specific finding in the original firmware, not evidence of how often
it occurs on a running dryer.

`src/ntc.c` now contains a corrected implementation: the search midpoint is
initialized and 0 °C is a normal return value. Both the C variant and standalone
firmware use the fix; the byte-identical baseline image retains the original.
Endpoints and saturation remain −20 and 199 °C.

All 65,536 uint16_t inputs were checked against the full original table.
All 4,096 ADC codes were compared with the original using four stack patterns:
16,372 calls returned the same value; 12 original calls failed to return within
2,000 instructions. These were ADC values 3959..3964, corresponding to 0 °C.
No other return-value differences were found. Report:
`analysis/standalone/ntc-tests.json`.

## Indeterminate configuration values in the original

Fan initialization at 0x080034A4 leaves parts of the timer structures unset.
The TIM1/TIM8 repetition counter and parts of TIM8 CR2/CCER then depend on
previous stack contents. For example, filling the stack with 0xFF produces
TIM8 CCER = 0xFFBB; the new initialization produces 0x0011, enabling the two
used channels without random extra bits.

I²C DMA initialization at 0x0800B8AC similarly inherits an indeterminate memory
address and transfer count. DMA is disabled at this point; both values are
set for each actual transfer. The new code initializes them to 0.

This deliberately defines behavior rather than reproducing the original stack
reads byte for byte. Differential initialization cases compare a shared zeroed
initial stack; a subsequent audit tests patterns 0, 0x55, 0xA5, and 0xFF and
lists differing registers. It is stored under `initialization_audit` in the
`hardware-tests.json` report for both variants.

## System library replacements

Standalone firmware uses bundled FreeRTOS 11.1.0 and the ARM GCC runtime.
It is not an instruction-by-instruction translation of the original RTOS.
Recovered configuration is in `include/FreeRTOSConfig.h`; sources and their
commit are in `vendor`. `src/reset.c` replaces the original reset and compressed
scatter initializer. See [standalone.md](standalone.md) for the memory map,
address translation, and small debug formatter.

## Test limitations

The basic differential suite compares eight functions through the final inserted
branches, checking return values, SP, and complete output structures. It covers
CRC, temperature bands, controller initialization, individual steps, and stateful
sequences. Calculations use ordinary finite values; they do not prove equivalence
for every NaN, infinity, unusual bit representation, or invalid pointer.
The new C uses libgcc soft-float, while the original firmware uses another runtime.

Other suites compare application tasks, commands, UI, outputs, and application
startup. RTOS and measurement acquisition responses are scripted; the UI also
uses a test rendering interface. Comparisons cover every application RAM byte
at `0x20000000..0x200006ff`, call order, and output register writes. Task stacks
cannot be compared because C compilation changes their layout. In tests,
`vTaskSuspend` returns immediately, modeling a subsequent external resume.
This does not verify real timing, task concurrency, or asynchronous timer callbacks.
The results must not be presented as emulation of the entire dryer.

The automatic Ghidra export initially omitted zero writes to GPIO aliases
`0x422181a8` and `0x422181ac` during bus recovery because the bit-band region
was not marked volatile. A differential test found the missing pulses; they
were restored from instructions `0x08008dd2..0x08008de2`. The Ghidra map and
saved export are now corrected. This demonstrates why pseudocode alone cannot
be treated as a completed reconstruction.

Separate suites also verify full rendering against a display memory model,
transfers against a GPIO model, and peripheral drivers against scripted status
bits. They check register access order, width, and values, timeouts, and tick
wraparound. The original display delay loop body is preserved instruction by
instruction, although the inserted function redirection adds overhead.

The emulator does not represent the whole board: actual interrupt timing,
heater electrical polarity, the display, airflow, and FreeRTOS scheduling during
longer calculations have not been verified. Both newly built variants require
hardware testing. Compilation and tests alone do not confirm safe temperature
control on a physical device.

`tools/test_system.py` executes actual standalone board and kernel initialization,
stopping before the first exception return into a task. Additional cases execute
real kernel APIs and a timer command. This suite also does not prove correct
asynchronous context switching on the device.

Other open questions include the exact MCU model, display controller designation,
physical actuator and chamber identities, and any external memory and its contents.
The baseline assembly preserves handlers that have not yet been named.
