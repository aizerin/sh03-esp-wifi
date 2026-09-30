# Reconstructed control logic

## Tasks

| Task | Entry point | Role |
| --- | --- | --- |
| `main` | `0x0800BD3C` | Hardware and UART initialization, Init_Task creation, scheduler startup |
| `Init_Task` | `0x08004EA4` | Queues, semaphores, two event groups, creation of the remaining tasks |
| `Task_KeyScan` | `0x08009E24` | Touch codes, short/long presses, UI handler queue |
| `Task_SensorDataUpdate` | `0x08009FB8` | GXHT30, NTC, fan speed, filtering, and error reporting |
| `Task_InteractiveProcess` | `0x080085BC` | Controls, profiles, display, drying start/pause, alarms |
| `Task_SystemMonitor` | `0x0800A4C4` | Sensor, fan speed, and heating progress checks |
| `Task_Drying` | `0x08007B34` | State machine and control for one chamber |
| `Task_CountDownTimer` | `0x080079DC` | Counting seconds for one chamber |
| `Task_SystemErrAlarm` | `0x0800A464` | Audible alarm |

`Task_Drying` is created separately for each chamber, uses a queue of 16-byte
messages, and creates its own countdown. Message commands are `0 = stop/pause`,
`1 = run`, and `2 = settings change/reset`. Mode `0` is timed drying; mode `1`
compares humidity against a threshold and uses a subsequent timed phase.
Starting this mode sets the duration to `0x1c20 = 7200 s`.

Reconstructed message format (offsets from `drying_send_command` and `Task_Drying`):

| Offset | Size | Meaning |
| --- | ---: | --- |
| 0 | 1 | Chamber 0/1 |
| 1 | 1 | Command |
| 2 | 1 | Mode |
| 3 | 1 | Material index |
| 4 | 1 | Displayed/averaged chamber temperature |
| 5 | 1 | Target temperature |
| 6 | 1 | Current humidity when preparing the message |
| 7 | 1 | Humidity threshold |
| 8 | 1 | NTC temperature converted to a byte |
| 9–11 | 3 | Reserved/alignment |
| 12 | 4 | Duration in seconds |

## Regulation

Two discrete PID controllers form a cascade. The outer loop uses the chamber
temperature and returns a target temperature for the inner loop. The inner
loop compares this value with the NTC temperature and returns heater output.

| Parameter | Outer loop | Inner loop |
| --- | ---: | ---: |
| Kp | 1.1 | 3.55 |
| Ki | 0.002 | 0.0031 |
| Kd | 0.00001 | 0.00066 |
| Output range | target to target + 5 | 0 to 50 |
| Initial integral | `float(double(target) * 0.9)` | 0 |
| Feed-forward | `(6 * lower_limit - 165) / 45` | 0 |

An output of 50 represents the original 50% PWM duty limit. `heater_set_duty`
multiplies the percentage by 40 and writes the compare value to TIM4; the
counter period is 4000. With a 72 MHz clock and a prescaler of 719, the PWM
period is 40 ms (25 Hz). This is calculated from firmware register settings,
not measured at the electrical output.

`Task_Drying` runs the inner control loop when the tick difference is **greater
than 200**, and the outer loop on every 25th such pass. The task's main loop
has a 50-tick delay. Exact periods of 200 ms and 5 s therefore cannot simply
be assumed; delays, peripheral calls, and scheduling affect execution.
At a nominal 72 MHz, one tick is 1 ms: SysTick reload is 71,999 (`0x0800D338`).

Integration limiting is updated after the calculation, based on the unsaturated
output. The inner loop also switches derivative_gate. Calculation order,
double precision during outer-loop initialization, and conversions through
`int32_t` are preserved in C and verified by differential tests.

## Sensors and filtering

A chamber snapshot at `0x200003B4 + 0x14 * chamber` contains five 32-bit fields:
`float temperature`, `int filtered_temperature`, `uint humidity`, `int NTC`, `uint rpm`.
GXHT30 is read at an interval derived from 1000 ticks; the task itself waits
150 ticks. The code includes an NTC-dependent temperature correction and a
30-element filter; their exact original order remains in the assembly and pseudocode.

Communication uses 8-bit I²C addresses `0x88/0x8A` (7-bit `0x44/0x45`).
Commands in the initial data are `24 16`, `30 A2`, and `30 93`. CRC starts at
`0xFF`, uses polynomial `0x31`, and has no final XOR. All 256 entries in the
original CRC table match this definition.

## Material profiles

The table at `0x08010BAC` contains ten eight-byte records: five name bytes
including NUL/padding, one temperature byte, and a little-endian uint16 duration
in seconds. Edit it in `config/materials.json`.

| Material | Temperature °C | Duration h |
| --- | ---: | ---: |
| PLA | 50 | 8 |
| PETG | 65 | 8 |
| ABS | 80 | 12 |
| TPU | 70 | 8 |
| PVA | 50 | 8 |
| ASA | 80 | 12 |
| PA | 80 | 12 |
| PP | 55 | 8 |
| PC | 80 | 12 |
| PET | 80 | 12 |

These are values found in the firmware, not new material recommendations.
