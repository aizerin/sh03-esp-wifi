/* Reconstructed from SH03 V3.5.1. Address-level provenance in docs/control.md.
 * Preserve the original calculation order and saturation/anti-windup behavior.
 * These are discrete coefficients; do not multiply by a new dt implicitly.
 */
#include "sh03_control.h"

void sh03_pid_init_outer(Sh03Pid *s,uint8_t target,uint8_t ceiling)
{
    *s=(Sh03Pid){0};
    s->derivative_gate=1.0f;
    s->derivative_filter=1.0f;
    s->proportional_weight=1.0f;
    s->kp=1.1f; s->ki=0.002f; s->kd=0.00001f;
    s->derivative_weight=1.0f;
    s->output_max=(float)ceiling; s->output_min=(float)target;
    /* Original uses double multiplication then converts to float. */
    s->integral_state=(float)((double)target*0.9);
    s->integral_gate=1.0f;
    s->feed_forward_gain=6.0f;
    s->feed_forward_offset=-165.0f;
    s->feed_forward_scale=45.0f;
}

void sh03_pid_init_inner(Sh03Pid *s)
{
    *s=(Sh03Pid){0};
    s->derivative_gate=1.0f;
    s->derivative_filter=1.0f;
    s->proportional_weight=1.0f;
    s->kp=3.55f; s->ki=0.0031f; s->kd=0.00066f;
    s->derivative_weight=1.0f;
    s->output_max=50.0f;
    s->integral_gate=1.0f;
}

void sh03_pid_calculate(Sh03Pid *s)
{
    s->proportional=s->proportional_weight*s->setpoint-s->measured;
    s->integral=s->ki*(s->integral_gate*(s->setpoint-s->measured))+s->integral_state;
    s->integral_state=s->integral;
    s->derivative_delta=s->kd*(s->derivative_gate*(s->setpoint*s->derivative_weight-s->measured))-s->derivative_delta;
    s->derivative=s->derivative_delta+s->derivative_state;
    s->derivative_state=s->derivative*s->derivative_filter;
    s->feed_forward=s->feed_forward_scale!=0.0f?
        (s->feed_forward_gain*s->output_min+s->feed_forward_offset)/s->feed_forward_scale:0.0f;
    s->unclamped=s->kp*((s->proportional+s->integral)+s->derivative)+s->feed_forward;
    float lower=s->unclamped<=s->output_min?s->output_min:s->unclamped;
    s->output=lower<=s->output_max?lower:s->output_max;
}

uint8_t sh03_pid_step(Sh03Pid *s,float setpoint,float measured)
{
    s->setpoint=setpoint;s->measured=measured;
    sh03_pid_calculate(s);
    return (uint8_t)(int32_t)s->output;
}

void sh03_control_outer_update(Sh03ChamberControl *s)
{
    s->cascade_setpoint=sh03_pid_step(&s->outer,(float)s->target_temperature,s->chamber_temperature);
    s->outer.integral_gate=!(s->outer.unclamped>=s->outer.output_max)&&
        s->outer.unclamped>=s->outer.output_min?1.0f:0.0f;
}

uint8_t sh03_control_inner_update(Sh03ChamberControl *s)
{
    uint8_t result=sh03_pid_step(&s->inner,(float)s->cascade_setpoint,(float)s->heater_temperature);
    float enabled=!(s->inner.unclamped>=s->inner.output_max)&&
        s->inner.unclamped>=s->inner.output_min?1.0f:0.0f;
    s->inner.integral_gate=enabled;s->inner.derivative_gate=enabled;
    return result;
}

uint8_t sh03_temperature_band(uint8_t chamber,int measured,int target)
{
    (void)chamber;
    if(measured<target-1)return 1;
    if(measured>target+1)return 2;
    return 3;
}

uint8_t sh03_sensor_crc8(const uint8_t *data,uint8_t count)
{
    uint8_t crc=0xff;
    for(unsigned i=0;i<count;i++) {
        crc^=data[i];
        for(unsigned bit=0;bit<8;bit++)crc=(uint8_t)((crc&0x80)?(crc<<1)^0x31:crc<<1);
    }
    return crc;
}
