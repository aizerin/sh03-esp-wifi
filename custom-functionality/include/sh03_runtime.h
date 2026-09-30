#ifndef SH03_RUNTIME_H
#define SH03_RUNTIME_H
#include "sh03_application.h"

/* Boundary to the remaining original firmware. Each callable address includes
 * the Thumb bit. These calls also go through replacement entry trampolines. */
#ifdef SH03_STANDALONE
uintptr_t sh03_resolve(uintptr_t original_address);
#define SH03_FN(address, result, ...) ((result (*)(__VA_ARGS__))(sh03_resolve(address) | 1u))
#else
#define SH03_FN(address, result, ...) ((result (*)(__VA_ARGS__))(uintptr_t)((address) | 1u))
#endif
#define fw_ticks SH03_FN(0x0800ef04, uint32_t, void)
#define fw_delay SH03_FN(0x0800d3cc, void, uint32_t)
#define fw_delay_until SH03_FN(0x0800eb24, int32_t, uint32_t *, uint32_t)
#define fw_enter_critical SH03_FN(0x0800d268, void, void)
#define fw_exit_critical SH03_FN(0x0800d298, void, void)
#define fw_mutex_create SH03_FN(0x0800e200, void *, uint8_t)
#define fw_semaphore_take SH03_FN(0x0800e770, int32_t, void *, uint32_t)
#define fw_queue_send SH03_FN(0x0800e378, int32_t, void *, const void *, uint32_t, int32_t)
#define fw_queue_receive SH03_FN(0x0800e61c, int32_t, void *, void *, uint32_t)
#define fw_event_set SH03_FN(0x0800df9c, uint32_t, void *, uint32_t)
#define fw_event_get SH03_FN(0x0800df44, uint32_t, void *)
#define fw_task_create SH03_FN(0x0800ead0, int32_t, void (*)(void *), const char *, uint16_t, void *, uint32_t, void **)
#define fw_task_suspend SH03_FN(0x0800da6c, void, void *)
#define fw_task_resume SH03_FN(0x0800d900, void, void *)
#define fw_task_state SH03_FN(0x0800b798, int32_t, void *)
#define fw_abort_delay SH03_FN(0x0800e910, int32_t, void *)
#define fw_printf SH03_FN(0x0800ae8c, int32_t, const char *, ...)
#define fw_sensor_snapshot SH03_FN(0x080045d4, void, Sh03SensorSnapshot *, uint8_t)
#define fw_control_initialize SH03_FN(0x0800ab48, void, const Sh03DryingMessage *)
#define fw_temperature_band SH03_FN(0x0800503c, uint8_t, uint8_t, int, int)
#define fw_pid_outer_init SH03_FN(0x08005660, void, Sh03Pid *, uint8_t, uint8_t)
#define fw_pid_inner_init SH03_FN(0x0800596c, void, Sh03Pid *)
#define fw_control_outer SH03_FN(0x08005608, void, Sh03ChamberControl *)
#define fw_control_inner SH03_FN(0x080058f0, uint8_t, Sh03ChamberControl *)
#define fw_actuators_request SH03_FN(0x08002564, void, uint8_t, uint8_t)
#define fw_actuators_set SH03_FN(0x08001938, void, uint8_t, uint8_t)
#define fw_actuator_schedule SH03_FN(0x08005e5c, void, uint8_t, uint8_t)
#define fw_actuator_pulse_a SH03_FN(0x08004730, void, uint8_t, uint8_t)
#define fw_actuator_pulse_b SH03_FN(0x080047c4, void, uint8_t, uint8_t)
#define fw_fan_enable SH03_FN(0x080025e8, void, uint8_t, uint8_t)
#define fw_heater_duty SH03_FN(0x0800690c, void, uint8_t, uint8_t)
#define fw_fan_enabled SH03_FN(0x08004410, uint8_t, uint8_t)
#define fw_humidity_elapsed SH03_FN(0x08004440, uint32_t, uint8_t)
#define fw_mode SH03_FN(0x0800445c, uint8_t, uint8_t)
#define fw_target_temperature SH03_FN(0x08004478, uint8_t, uint8_t)
#define fw_ui_running SH03_FN(0x08004494, uint8_t, uint8_t)
#define fw_running SH03_FN(0x08004638, uint8_t, uint8_t)
#define fw_band SH03_FN(0x08004658, uint8_t, uint8_t)
#define fw_elapsed SH03_FN(0x08004674, uint32_t, uint8_t)
#define fw_actuators_self_test SH03_FN(0x080018fc, void, void)
#define fw_hall_read SH03_FN(0x080044b0, uint8_t, uint8_t, uint16_t *)
#define fw_latch_fault SH03_FN(0x080069e0, void, uint8_t, uint8_t)
#define fw_fault SH03_FN(0x0800461c, uint8_t, uint8_t)
#define fw_touch_read SH03_FN(0x0800b358, uint8_t, void)
#define fw_sensors_initialize SH03_FN(0x08004068, void, void)
#define fw_gxht_read SH03_FN(0x08003bb8, uint8_t, uint8_t, float *, uint32_t *)
#define fw_ntc_temperature SH03_FN(0x080045bc, int32_t, uint8_t)
#define fw_fan_rpm SH03_FN(0x080043c4, uint32_t, uint8_t)

#define SH03_MEM(type, address) (*(type volatile *)(uintptr_t)(address))
#define SH03_U8(address) SH03_MEM(uint8_t, address)
#define SH03_U32(address) SH03_MEM(uint32_t, address)
#define SH03_HANDLE(address) SH03_MEM(void *, address)
#define sh03_controls ((Sh03ChamberControl *)0x200004fc)
#define sh03_sensors ((Sh03SensorSnapshot *)0x200003b4)
#define sh03_filters ((Sh03TemperatureFilter *)0x200003dc)
#define sh03_messages ((Sh03DryingMessage *)0x20000344)
#define sh03_requests ((volatile Sh03ActuatorRequest *)0x20000308)
#define sh03_diagnostic SH03_U8(0x20000318)
#define sh03_elapsed(ch) SH03_U32(0x2000069c + 4u * (ch))
#define sh03_humidity_elapsed(ch) SH03_U32(0x2000031c + 4u * (ch))
#define sh03_events(ch) SH03_HANDLE(0x2000032c + 4u * (ch))
#define sh03_drying_queue(ch) SH03_HANDLE(0x20000364 + 4u * (ch))
#define sh03_message_ack(ch) SH03_HANDLE(0x20000324 + 4u * (ch))
#define sh03_drying_task(ch) SH03_HANDLE(0x200004ec + 4u * (ch))
#define sh03_mode(ch) SH03_U8(0x2000001c + 8u * (ch))
#define sh03_ui_running(ch) SH03_U8(0x2000001d + 8u * (ch))
#define sh03_ui_field(ch, offset) SH03_U8(0x20000374 + 16u * (ch) + (offset))
#define sh03_ui_duration(ch) SH03_U32(0x2000037c + 16u * (ch))

void sh03_fault_stop_heaters(void);

static inline void sh03_reset_elapsed(uint8_t chamber)
{
    sh03_elapsed(chamber) = 0;
    sh03_humidity_elapsed(chamber) = 0;
}
static inline void sh03_mutex_give(void *mutex)
{
    fw_queue_send(mutex, (void *)0, 0, 0);
}
#endif
