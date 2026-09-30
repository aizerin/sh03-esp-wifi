#include "sh03_runtime.h"

void sh03_sensor_snapshot(Sh03SensorSnapshot *output, uint8_t chamber)
{
    *output = sh03_sensors[chamber];
}

/* The NTC term is truncated as an integer before conversion to float. */
static float compensated_temperature(const Sh03SensorSnapshot *s)
{
    return (s->temperature + s->temperature) / 10.0f + (float)((s->heater_temperature * 8) / 10);
}

/* 0x08009fb8. Invalid-sample counter is shared between both chambers in the
 * original. Keep the shared counter and the original 30-sample filter order. */
void sh03_task_sensor_update(void *argument)
{
    (void)argument;
    uint8_t invalid_samples = 0;
    uint32_t last_sample[2] = {0, 0};
    fw_sensors_initialize();
    for (uint8_t chamber = 0; chamber < 2; ++chamber) {
        Sh03SensorSnapshot *s = &sh03_sensors[chamber];
        fw_gxht_read(chamber, &s->temperature, &s->humidity);
        s->heater_temperature = fw_ntc_temperature(chamber);
        s->filtered_temperature = (int32_t)s->temperature;
        s->filtered_temperature = (int32_t)compensated_temperature(s);
        sh03_filters[chamber] = (Sh03TemperatureFilter){0};
    }
    for (;;) {
        uint32_t now = fw_ticks();
        if (fw_semaphore_take(SH03_HANDLE(0x20000334), 10) == 1) {
            for (uint8_t chamber = 0; chamber < 2; ++chamber) {
                Sh03SensorSnapshot *s = &sh03_sensors[chamber];
                s->heater_temperature = fw_ntc_temperature(chamber);
                if (sh03_ui_running(chamber) == 1) s->fan_rpm = fw_fan_rpm(chamber);
                if (now - last_sample[chamber] > 1000) {
                    last_sample[chamber] = now;
                    if (fw_gxht_read(chamber, &s->temperature, &s->humidity) == 1) {
                        if (s->humidity == 0 || (int32_t)s->temperature == 0) {
                            if (++invalid_samples > 5) {
                                invalid_samples = 0;
                                if (fw_fault(chamber) == 10) fw_event_set(sh03_events(chamber), 0x20);
                            }
                        } else invalid_samples = 0;
                        if (s->temperature != 0.0f) s->temperature = compensated_temperature(s);
                        Sh03TemperatureFilter *filter = &sh03_filters[chamber];
                        if (filter->count < 30) ++filter->count;
                        if (filter->count == 30) filter->sum -= filter->samples[filter->next];
                        filter->sum += (int32_t)s->temperature;
                        filter->samples[filter->next] = (int32_t)s->temperature;
                        filter->next = (filter->next + 1) % 30;
                        int32_t average = filter->sum / filter->count;
                        s->filtered_temperature = average < 0 ? 0 : average > 99 ? 99 : average;
                    } else fw_delay(10);
                }
            }
            sh03_mutex_give(SH03_HANDLE(0x20000334));
        }
        fw_delay(150);
    }
}
