#ifndef SH03_APPLICATION_H
#define SH03_APPLICATION_H
#include "sh03_control.h"

/* Shared layouts are fixed by the original application and its RTOS tasks. */
typedef struct {
    uint8_t chamber, command, mode, material;
    uint8_t chamber_temperature, target_temperature, humidity, humidity_threshold;
    uint8_t heater_temperature, reserved[3];
    uint32_t duration_seconds;
} Sh03DryingMessage;

typedef struct {
    float temperature;
    int32_t filtered_temperature;
    uint32_t humidity;
    int32_t heater_temperature;
    uint32_t fan_rpm;
} Sh03SensorSnapshot;

typedef struct {
    uint8_t pending, enabled, reserved[2];
    uint32_t deadline;
} Sh03ActuatorRequest;

typedef struct {
    uint8_t count, next, reserved[2];
    int32_t samples[30], sum;
} Sh03TemperatureFilter;

enum { SH03_TIMED = 0, SH03_HUMIDITY = 1 };
enum { SH03_STOP = 0, SH03_RUN = 1, SH03_RESET = 2 };
_Static_assert(sizeof(Sh03DryingMessage) == 16, "drying message ABI");
_Static_assert(offsetof(Sh03DryingMessage,duration_seconds) == 12, "duration ABI");
_Static_assert(sizeof(Sh03SensorSnapshot) == 20, "sensor snapshot ABI");
_Static_assert(sizeof(Sh03ActuatorRequest) == 8, "actuator request ABI");
_Static_assert(sizeof(Sh03TemperatureFilter) == 128, "temperature filter ABI");

void sh03_control_initialize_from_message(const Sh03DryingMessage *message);
void sh03_task_drying(void *argument);
void sh03_task_countdown(void *argument);
void sh03_task_key_scan(void *argument);
void sh03_task_sensor_update(void *argument);
void sh03_task_alarm(void *argument);
void sh03_task_system_monitor(void *argument);
void sh03_actuators_self_test(void);
uint8_t sh03_drying_send_command(uint8_t chamber, uint8_t run);
void sh03_sensor_snapshot(Sh03SensorSnapshot *output, uint8_t chamber);
void sh03_actuator_schedule(uint8_t chamber, uint8_t enabled);
void sh03_chamber_latch_fault(uint8_t chamber, uint8_t fault);
void sh03_fan_enable(uint8_t chamber, uint8_t enabled);
void sh03_heater_set_duty(uint8_t chamber, uint8_t percent);
void sh03_actuator_pulse_a(uint8_t chamber, uint8_t actuator);
void sh03_actuator_pulse_b(uint8_t chamber, uint8_t actuator);
void sh03_chamber_actuators_set(uint8_t chamber, uint8_t enabled);
void sh03_chamber_actuators_request(uint8_t chamber, uint8_t enabled);
#endif
