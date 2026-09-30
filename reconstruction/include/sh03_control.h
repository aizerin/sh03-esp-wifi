#ifndef SH03_CONTROL_H
#define SH03_CONTROL_H
#include <stdint.h>
#include <stddef.h>

/* Recovered layout at chamber_state + 0x0c and +0x6c. Names are descriptive. */
typedef struct {
    float setpoint, measured, output;
    float derivative_gate, derivative_filter, proportional_weight;
    float kp, ki, kd, derivative_weight;
    float output_max, output_min;
    float proportional, integral, derivative, unclamped;
    float integral_state, derivative_state, derivative_delta, integral_gate;
    float feed_forward, feed_forward_gain, feed_forward_offset, feed_forward_scale;
} Sh03Pid;

typedef struct {
    uint8_t temperature_band, running, countdown_enabled, heater_temperature;
    uint8_t cascade_setpoint, target_temperature, reserved_06[2];
    float chamber_temperature;
    Sh03Pid outer, inner;
    uint32_t reserved_cc;
} Sh03ChamberControl;

_Static_assert(sizeof(Sh03Pid)==0x60,"PID ABI");
_Static_assert(offsetof(Sh03ChamberControl,outer)==0x0c,"outer PID ABI");
_Static_assert(offsetof(Sh03ChamberControl,inner)==0x6c,"inner PID ABI");
_Static_assert(sizeof(Sh03ChamberControl)==0xd0,"chamber control ABI");

void sh03_pid_init_outer(Sh03Pid *,uint8_t target,uint8_t ceiling);
void sh03_pid_init_inner(Sh03Pid *);
void sh03_pid_calculate(Sh03Pid *);
uint8_t sh03_pid_step(Sh03Pid *,float setpoint,float measured);
void sh03_control_outer_update(Sh03ChamberControl *);
uint8_t sh03_control_inner_update(Sh03ChamberControl *);
uint8_t sh03_temperature_band(uint8_t chamber,int measured,int target);
uint8_t sh03_sensor_crc8(const uint8_t *,uint8_t count);

#endif
