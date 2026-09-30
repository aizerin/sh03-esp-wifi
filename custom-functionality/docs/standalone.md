# Standalone build

`tools/build_standalone.py` compiles the C application, bundled FreeRTOS 11.1.0,
and JSON data tables. The `standalone.ld` linker script creates a new image
starting at 0x08000000. No dump, Ghidra, or firmware.S is read; this is verified
by `tools/test_build.py` in a separate directory.

## Memory and references

- 0x08000000: 76 interrupt vectors; reset and used IRQs have new symbols.
  SVC, PendSV, and SysTick point to the FreeRTOS Cortex-M3 port.
- Code and ordinary constants are linked normally. Original data tables remain
  at 0x08010b74..0x08011073; the linker checks for overlap with code.
- 0x20000000..0x200006ff: reserved application RAM with original offsets.
  The initial 680 bytes are restored from config/initial-state.json.
- From 0x20000700: normally linked data and BSS, including FreeRTOS.
  The heap retains its original 20 KiB size. Initial SP is 0x20010000 for the
  detected 64 KiB RAM; the linker reserves at least 4 KiB for the main/IRQ stack.

Addresses in SH03_FN identify original functions for analysis traceability.
The standalone build translates them to new symbols through sh03_resolve.
Execution does not enter original code addresses. PWM function pointers in
hardware descriptors follow the same route. The script checks coverage of
explicit calls before linking.

The default make builds the standalone image. Run it again after changing C
sources or configuration profiles.

## FreeRTOS

Sources come from the official
[FreeRTOS-Kernel V11.1.0](https://github.com/FreeRTOS/FreeRTOS-Kernel/tree/V11.1.0),
commit dbf70559b27d39c1fdb68dfb9a32140b6a6777a0. Source copies, the license,
and individual file hashes are in vendor/FreeRTOS-Kernel.

The configuration restores 72 MHz, a 1 kHz tick, 15 priorities, the BASEPRI=0x50
system call mask, a 20 KiB heap_4, a 256-word idle stack, a 512-word timer stack,
timer priority 14, and a ten-command queue. It uses one task notification and
stack pattern checking. Address evidence is in the include/FreeRTOSConfig.h
comment and original functions 0x0800D338, 0x0800C28C, 0x0800F7C8, 0x0800C0B4,
0x0800C428, and 0x0800DBDC.

## Runtime

Original compiler and formatting routines are not carried over as embedded
machine code. Standard libgcc from the ARM toolchain supplies soft-float and
other compiler-required operations. src/freestanding.c contains memory and
string operations. src/debug.c implements the application's %d, %s, and %%
formats, with a limit of 255 transmitted characters. Extend the formatter
to add other formats.

The timer interface in `src/rtos_adapter.c` checks for a null handle before
calling FreeRTOS. The original panel queried timer activity before creating
the timer. Reading through NULL happened to return zero from reserved vectors
in the original image; after adding the OTA bootloader, it read different data
and incorrectly marked the timer active. This caused missing blinking and
commands with invalid handles in the timer task queue.

The settings timeout timer is now only stopped and restarted. After expiration,
it remains allocated as an inactive one-shot timer. The panel therefore avoids
using a freed handle, and repeated editing does not create additional timers.
The blink interval remains 500 ms and the settings timeout remains ten seconds
(the original 10005 ms for the second button).

Exception and stack overflow handlers force both heater control pins inactive
before halting. They do not need the scheduler or initialized descriptors and
leave fan control unchanged. This is not a watchdog and does not handle arbitrary
hangs that do not trigger an exception.

The remaining assembly is limited to Cortex-M3 architectural instructions in
the RTOS port, BASEPRI manipulation in IRQs, and the original 32-byte display
delay loop. These are short, named inline assembly sections in the sources,
not an embedded image of the original firmware.

## Verification

`make test-standalone` checks reset, FreeRTOS APIs, and an independent source-only
build.

tools/test_system.py also executes actual reset and board setup, task creation,
and scheduler preparation. It then uses real compiled FreeRTOS APIs, including
software timer command processing. The test stops before the first context
switch and does not replace a board test.

`make test-ota` includes `tools/test_ui_rtos.py`: it executes ARM panel code,
real FreeRTOS queues, mutexes, the timer task, and callbacks, with boot flash
also mapped at address zero. It checks NULL/freed handles, blinking, chamber
switching, settings timeout and repeated editing without leaks, start/stop of
both chambers, and heater shutdown in fault handlers. Time and task handoffs
are scripted; this does not verify real interrupts, preemption, the display,
or physical heating.

`tools/test_remote_display.py` connects the real UART parser, panel, control
tasks, and FreeRTOS to a memory model of both LCDs. It also checks resulting
segments after settings changes, sensor updates, start, stop, and restart in
both Timed and Humidity modes. It verifies that commands for one chamber do
not light the other and that duplicate or rejected commands do not disrupt
local editing. Older UART code forced both chambers to render after every
successful ACK even though their visibility flag remained zero. Subsequent
sensor updates therefore erased individual values. Remote settings now use
the same visibility state and incremental renderer as the panel, without
enabling editing mode. When checking RUN/PAUSE/END events, the panel tests
the relevant bit instead of comparing the entire value. FreeRTOS returns all
event-group bits; flag `0x80`, which remains set in Humidity mode, previously
prevented events `0x81`/`0x82`/`0x88` from being processed, so the panel could
continue to indicate running after a stop.
