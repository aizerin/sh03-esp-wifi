# Reconstruction status

> Historical analysis record. For the current test suite and available commands,
> see [workflow.md](workflow.md).


**The entire firmware can be built as a standalone source project.** The default
`make` produces `build/standalone/firmware.bin`, `.hex`, and `.elf` from C sources,
configuration tables, bundled FreeRTOS 11.1.0, and the ARM GCC runtime.
This build does not use the original dump or the extensive assembly listing.

The reconstructed application includes the drying state machine, both PID loops,
timing, panel controls, display fonts and transfers, all sensor acquisition,
fault detection, output control, and hardware configuration. Reset, initial RAM,
interrupt vectors, clocks, drivers, and the runtime interface are also provided.

The intermediate `firmware-c.bin` variant replaces 198 individual original function
entry points and retains the original RTOS/runtime. This count is not a measure
of standalone project completeness: its system libraries were replaced with entire
source modules, rather than individual rewrites of every internal decompiler
function. `build/standalone/manifest.json` records mappings from original addresses
to new symbols.

## Evidence

- The original assembly still builds into the same 256 KiB as the backup.
- Standalone builds work without the dump, analysis, previous builds, or `.S` files.
  Changing the PLA profile from 50 to 51 °C changes the corresponding byte.
- 32,429 differential cases compare results, application RAM, register accesses,
  and call order against the original.
- 65,554 NTC checks cover every uint16_t input and both chambers. A separate
  original-firmware audit covers all 4,096 ADC codes and four initial stack patterns.
- Task comparisons cover 13,664 steps.
- Actual reset and startup create the initialization, idle, and timer tasks and
  prepare the scheduler with a 1 ms tick. The test stops before the first context
  switch. Real FreeRTOS APIs are also tested for queues, semaphores, mutexes,
  events, notifications, task lifecycle, and timers.

Current hashes and results: `analysis/c-coverage.json`
and `analysis/standalone`. The completion flag is generated
only when hashes match across all required reports, including the independent
build; it does not mean a physical board test has been performed.

## Scope of the result

This is neither the manufacturer's original source project nor a byte-identical
build. Inferred functions and interfaces are preserved, with explicitly documented
fixes for NTC conversion and indeterminate configuration fields.
See [limitations.md](limitations.md).

Hardware testing, actual bus timing, task concurrency, and the device's temperature
response remain unverified. The exact MCU manufacturer, display controller type,
and physical assignment of the two chambers still require board inspection.
