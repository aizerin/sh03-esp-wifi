#include "sh03_runtime.h"

void sh03_actuator_schedule(uint8_t chamber, uint8_t enabled)
{
    if (sh03_requests[chamber].pending == 0) {
        for (uint8_t other = 0; other < 2; ++other) {
            if (other == chamber) continue;
            if (sh03_requests[other].pending == 1) sh03_requests[chamber].deadline = sh03_requests[other].deadline + 100;
            else if (sh03_requests[other].pending == 0) sh03_requests[chamber].deadline = fw_ticks() + chamber * 100u + 100;
        }
        sh03_requests[chamber].pending = 1;
        sh03_requests[chamber].enabled = enabled;
    }
}

void sh03_chamber_latch_fault(uint8_t chamber, uint8_t fault)
{
    if (SH03_U8(0x2000002d + 2u * chamber) != 1) {
        if (fault != 10) SH03_U8(0x2000002d + 2u * chamber) = 1;
        SH03_U8(0x2000002c + 2u * chamber) = fault;
    }
}

void sh03_fan_enable(uint8_t chamber, uint8_t enabled)
{
    uintptr_t descriptor = 0x20000054 + 0x50u * chamber;
    void (*compare)(uint32_t, uint16_t) = SH03_FN(SH03_U32(descriptor + 8),void,uint32_t,uint16_t);
    compare(SH03_U32(descriptor), enabled == 1 ? 849 : 0);
    SH03_U8(0x20000080 + 0x50u * chamber) = enabled;
}

void sh03_heater_set_duty(uint8_t chamber, uint8_t percent)
{
    uintptr_t descriptor = 0x200001ec + 0x18u * chamber;
    void (*compare)(uint32_t, uint16_t) = SH03_FN(SH03_U32(descriptor + 8),void,uint32_t,uint16_t);
    compare(SH03_U32(descriptor), (uint16_t)(percent * 40u));
}

static uintptr_t actuator_alias(uint8_t chamber, uint8_t actuator, unsigned direction_offset)
{
    uintptr_t descriptor = 0x200000f0 + chamber * 0x30u + actuator * 0x18u + direction_offset;
    uint32_t odr = SH03_U32(descriptor + 4) + 12;
    return (odr & 0xf0000000u) + ((odr & 0xfffff) << 5) + SH03_U8(descriptor + 10) * 4u + 0x02000000u;
}

void sh03_actuator_pulse_a(uint8_t chamber, uint8_t actuator)
{
    SH03_U32(actuator_alias(chamber, actuator, 0)) = 1;
    fw_delay(65);
    SH03_U32(actuator_alias(chamber, actuator, 0)) = 0;
}

void sh03_actuator_pulse_b(uint8_t chamber, uint8_t actuator)
{
    SH03_U32(actuator_alias(chamber, actuator, 12)) = 1;
    fw_delay(65);
    SH03_U32(actuator_alias(chamber, actuator, 12)) = 0;
}

void sh03_chamber_actuators_set(uint8_t chamber, uint8_t enabled)
{
    if (enabled == 1) {
        fw_actuator_pulse_b(chamber, 0);
        fw_actuator_pulse_b(chamber, 1);
    } else {
        fw_actuator_pulse_a(chamber, 0);
        fw_actuator_pulse_a(chamber, 1);
    }
}

void sh03_chamber_actuators_request(uint8_t chamber, uint8_t enabled)
{
    fw_actuators_set(chamber, enabled);
}

void sh03_task_alarm(void *argument)
{
    (void)argument;
    for (;;) {
        for (unsigned pulse = 0; pulse < 6; ++pulse) {
            SH03_U32(0x422181b0) = 1;
            fw_delay(60);
            SH03_U32(0x422181b0) = 0;
            fw_delay(60);
        }
        fw_delay(1500);
    }
}
