# SH03 UART protocol v1

Dryer UART4: PC10 TX, PC11 RX, 115200 baud, 8 data bits, no parity, 1 stop bit,
3.3 V logic. It replaces text debug output. ESPHome is the client; the dryer
sends each chamber's state once per second. Drying remains autonomous if the
connection is lost. The ESPHome implementation is directly in `esphome/sh03.yaml`
using lambdas: `script sh03_command` sends messages, a 10 ms interval receives
and processes data, and a 5 s interval sends a query. No custom component is needed.

The `make ota` variant adds messages `0x10–0x18` for application updates.
They use the same frame; see the [OTA documentation](ota.md) for the format
and transactions. During updates, the YAML pauses regular queries and commands.

## Frame

All multibyte numbers are little endian. C struct packing is not used.

| Offset | Length | Contents |
| --- | ---: | --- |
| 0 | 2 | `A5 5A` |
| 2 | 1 | Version `01` |
| 3 | 1 | Message type |
| 4 | 2 | Sequence number |
| 6 | 1 | Payload length, maximum 64 |
| 7 | N | Payload |
| 7+N | 2 | CRC-16/CCITT-FALSE over offsets 2 through 6+N |

CRC: polynomial `0x1021`, initial value `0xFFFF`, no reflection or final XOR.
ASCII check vector `123456789` → `0x29B1`. Maximum frame size is 73 bytes.
Invalid versions, CRCs, and excessive lengths are discarded. The parser searches
for the next sync; incomplete messages are discarded after 500 ms of inactivity.
RX overrun/framing/parity/noise errors or buffer overflow discard all pending
input. At most one frame is processed per UI iteration (normally 100 ms); the
ISR only moves bytes. RX and TX each have a 256-byte ring buffer.

## Commands and acknowledgments

Type `2` (COMMAND), payload length 6:
`chamber:u8, operation:u8, value:u32`. Chamber indices are **0 and 1**.

| Operation | Value |
| --- | --- |
| 1 RUN | 0 stop, 1 start |
| 2 TEMPERATURE | 45–85 °C |
| 3 DURATION | 7200–352800 seconds, a multiple of 7200; Timed only |
| 4 MODE | 0 Timed, 1 Humidity |
| 5 HUMIDITY | 20–40%, a multiple of 5 |
| 6 MATERIAL | 0–9 according to `config/materials.json` |
| 7 DISPLAY | 0 turn off both displays including backlight, 1 turn on; indices 0 and 1 control the same shared switch |
| 8 DISPLAY_TIMEOUT (removed) | For older clients: 0 is accepted without changes; any nonzero value returns Unsupported. The timer no longer exists. |

Drying settings can be changed only while the chamber is stopped. Selecting a
material also sets its temperature and duration. Changing mode sets the default
duration; Humidity uses 7200 s. Accepted settings display the relevant chamber's
values without enabling local editing mode or its timer. This does not turn on
the other panel. Values remain consistent through subsequent sensor updates;
repeated ACKs and STOP commands do not force the displays on. Restarting drying
also restores the chamber display. Local editing and diagnostics block remote
start and settings changes. Start is rejected during the first two seconds after
boot, for a latched fault, or for invalid sensor inputs. Remote commands never
clear a fault. Stop is allowed during faults or editing; the queue/semaphore may
return Busy. Accepting stop means delivery to the control task, not an emergency
hardware disconnection.

DISPLAY is independent of drying: it is allowed during operation, local editing,
diagnostics, or faults. It creates no heater control message and does not change
drying settings or timers. OFF drives shared backlight PB1 LOW and disables both
LCD outputs. Their RAM continues updating, so ON shows current state without
missing segments; the backlight is enabled only after both LCDs. The first touch
when displays are off only wakes them and does not perform its original action,
even for a long press. The next touch works normally.

**Automatic turn-off has been removed.** After switching on in HA or waking by
touch, displays stay on until DISPLAY=0. Telemetry, QUERY, sensor updates, and
remote drying commands do not wake dark displays. LCD bus access shares a mutex
with rendering. Displays are on after an SH03 restart; this state is neither
saved to flash nor restored from ESPHome.

Type `3` (ACK), same sequence number as the command, payload length 3:
`chamber:u8, operation:u8, result:u8`.

| Result | Meaning |
| --- | --- |
| 0 | Accepted (actual state follows in telemetry) |
| 1 | Invalid chamber index or value |
| 2 | Busy: running, editing, diagnostics, or occupied queue |
| 3 | Fault or invalid sensor value |
| 4 | Dryer is initializing |
| 5 | Unsupported operation/mode |

The last command with the same sequence number **and entire payload** is recognized
as a duplicate for five seconds: it returns the original ACK without executing
again. Invalid lengths and unknown message types are ignored. ESPHome allows
at most one unacknowledged command and reports a timeout after two seconds.
It neither automatically retries commands nor stores them for later execution.

Type `4` (QUERY) has an empty payload and requests both chamber states and the
display configuration without an ACK. ESPHome sends it every five seconds;
the dryer also streams without queries.

Type `5` (DISPLAY_CONFIG) remains only for already installed ESPHome clients:
its two-byte payload, `auto_off_seconds:u16`, is always **0** (disabled).
The dryer sends it after both STATE frames every second and after QUERY or
COMMAND. The new YAML ignores this message and provides no timer entity.
Older clients show 0 and attempts to enable the timer are rejected; the DISPLAY
switch continues working. STATE remains 30 bytes and other operation numbers
are unchanged.

## Chamber state

Type `1` (STATE), payload length 30. The sequence is a counter from the dryer.

| Payload offset | Type | Contents |
| --- | --- | --- |
| 0 | u8 | Chamber 0/1 |
| 1 | u8 | Flags: bit 0 program active, 1 drying, 2 countdown running, 3 fan enabled, 4 both displays on, 5 firmware supports DISPLAY |
| 2 | u8 | Mode |
| 3 | u8 | Material |
| 4 | u8 | Temperature setting °C |
| 5 | u8 | Humidity threshold % |
| 6 | u8 | Original fault code; 10 = no fault |
| 7 | u8 | Requested heater duty % |
| 8 | i16 | Filtered display temperature × 10 |
| 10 | u16 | Humidity % |
| 12 | i16 | NTC temperature °C |
| 14 | u32 | Fan revolutions/min; 0 when fan is disabled |
| 18 | u32 | Total duration setting, s |
| 22 | u32 | Elapsed countdown, s |
| 26 | u32 | Remaining time, s; saturated at zero |

Temperature is filtered by the original display algorithm, not the raw GXHT30
output. Heater duty is a control request, not a measurement of actual power draw.
In Humidity mode, the program can be active with heating off. Settings remain
visible after stop; mode and profiles are not written to flash.

Bit 4 is meaningful only when bit 5 is set. Older firmware sends zero in both
and rejects the new command as Unsupported. Display state is shared and identical
in both chambers' telemetry. Original ESPHome configurations ignore unknown bits.

### Display controls in ESPHome

Update SH03 using `build/ota/application.sh03` and the ESP32 using the new
`esphome/sh03.yaml`. Home Assistant provides **Displays enabled**, controlling
both LCDs and their backlight. The **Display auto-off** number is no longer in
the YAML. The switch confirms state exclusively from SH03 telemetry, so it also
reflects touch wake-up. ACK alone does not change the entity state. New commands
are not sent when communication is unavailable or an OTA transfer is in progress.

For `filament-dryer-01`, copy the **entire current
[esphome/sh03.yaml](../esphome/sh03.yaml)** into ESPHome Device Builder.
It already contains your pins, both Wi-Fi networks, power settings, and references
to the same secrets. Keep the existing `secrets.yaml` unchanged; individual
switches, number entities, and receive lambdas do not need to be merged manually.

If both chambers have not provided fresh state within five seconds, the shared
`UART connected` entity is off. Commands require fresh state from their chamber.
Reconnection only restores telemetry; it does not automatically start anything.
