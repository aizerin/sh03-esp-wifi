/* Recovered system monitor (0x0800a4c4). Faults latch once; shutdown and the
 * user-visible alarm are subsequently handled by the interactive task. */
#include "sh03_runtime.h"

typedef struct {
    uint8_t count, next;
    int16_t sum, changes[29], direction;
} TemperatureTrend;
_Static_assert(sizeof(TemperatureTrend) == 64, "monitor trend layout");

void sh03_actuators_self_test(void)
{
    fw_actuators_set(0, 1);
    fw_actuators_set(1, 1);
    fw_delay(300);
    fw_actuators_set(0, 0);
    fw_actuators_set(1, 0);
    fw_delay(100);
}

static void update_trend(TemperatureTrend *trend, int32_t measured, int32_t *previous)
{
    if (trend->count < 30) ++trend->count;
    else trend->sum -= trend->changes[trend->next];
    if (trend->count > 1) {
        trend->changes[trend->next] = (int16_t)((uint16_t)measured - (uint16_t)*previous);
        trend->sum += trend->changes[trend->next];
    }
    *previous = measured;
    trend->next = (trend->next + 1) % 29;
    /* Original Cortex-M3 SDIV with a zero divisor returns zero (DIV_0_TRP is
     * unset). Make that first-sample behavior explicit instead of C UB. */
    int rate = trend->count == 1 ? 0 : (trend->sum * 100) / (trend->count - 1);
    trend->direction = rate < 0 ? -1 : rate > 0 ? 1 : 0;
}

void sh03_task_system_monitor(void *argument)
{
    (void)argument;
    int32_t previous[2] = {0, 0};
    uint8_t ntc_failures[2] = {0, 0}, fan_failures[2] = {0, 0};
    uint32_t drift_started[2] = {0, 0};
    void *mutex = fw_mutex_create(1);
    fw_latch_fault(0, 10);
    fw_latch_fault(1, 10);
    TemperatureTrend trend[2] = {{0}, {0}};
    fw_actuators_self_test();
    for (;;) {
        if (fw_semaphore_take(mutex, 0) == 1) {
            for (uint8_t ch = 0; ch < 2; ++ch) {
                if (fw_fault(ch) != 10) continue;
                Sh03SensorSnapshot sample;
                fw_sensor_snapshot(&sample, ch);
                if (sample.heater_temperature < -18) {
                    if (++ntc_failures[ch] > 5) {
                        fw_latch_fault(ch, 5);
                        ntc_failures[ch] = 0;
                    }
                } else if (sample.heater_temperature >= 199) {
                    if (++ntc_failures[ch] > 5) {
                        fw_latch_fault(ch, 4);
                        ntc_failures[ch] = 0;
                    }
                } else ntc_failures[ch] = 0;
                if (fw_ui_running(ch) == 1) {
                    if (fw_fan_enabled(ch) == 1) {
                        if (sample.fan_rpm == 0) {
                            if (++fan_failures[ch] > 5) {
                                fw_latch_fault(ch, 1);
                                fan_failures[ch] = 0;
                            }
                        } else if (sample.fan_rpm <= 3499) {
                            if (++fan_failures[ch] > 5) {
                                fw_latch_fault(ch, 0);
                                fan_failures[ch] = 0;
                            }
                        } else fan_failures[ch] = 0;
                    }
                    uint32_t seconds = fw_mode(ch) == 1 ? fw_humidity_elapsed(ch) : fw_elapsed(ch);
                    if (fw_running(ch) == 1) {
                        update_trend(&trend[ch], sample.filtered_temperature, &previous[ch]);
                        int direction = trend[ch].direction;
                        if (seconds <= 60) drift_started[ch] = 0;
                        else {
                            int heater = sample.heater_temperature;
                            if (heater >= 111) fw_latch_fault(ch, 2);
                            else if (heater < 31) fw_latch_fault(ch, 3);
                            uint8_t band = fw_band(ch);
                            if (band == 2) {
                                if (drift_started[ch] == 0) {
                                    if (direction > 0) drift_started[ch] = seconds;
                                } else if (direction < 0) drift_started[ch] = 0;
                                else if (seconds - drift_started[ch] > 150) {
                                    if (heater > fw_target_temperature(ch) + 15 && direction >= 0) fw_latch_fault(ch, 2);
                                    drift_started[ch] = 0;
                                }
                            } else if (band == 1) {
                                if (drift_started[ch] == 0) {
                                    if (direction < 0) drift_started[ch] = seconds;
                                } else if (seconds - drift_started[ch] > 150) {
                                    if (heater < fw_target_temperature(ch) - 15 && direction <= 0) fw_latch_fault(ch, 3);
                                    drift_started[ch] = 0;
                                }
                            } else if (band == 3) {
                                if (drift_started[ch] == 0) {
                                    if (heater > fw_target_temperature(ch) + 10 || heater < fw_target_temperature(ch) - 10) drift_started[ch] = seconds;
                                } else if (heater < fw_target_temperature(ch) + 10 && heater > fw_target_temperature(ch) - 10) drift_started[ch] = 0;
                                else if (seconds - drift_started[ch] > 120) {
                                    if (heater > fw_target_temperature(ch) + 15 && direction >= 0) fw_latch_fault(ch, 2);
                                    else if (heater < fw_target_temperature(ch) - 15 && direction <= 0) fw_latch_fault(ch, 3);
                                    drift_started[ch] = 0;
                                }
                            }
                        }
                    } else drift_started[ch] = 0;
                }
                uint32_t events = fw_event_get(sh03_events(ch));
                if (events & 0x20) {
                    fw_latch_fault(ch, 8);
                    fw_event_get(sh03_events(ch));
                } else if (events & 0x40) {
                    fw_latch_fault(ch, 9);
                    fw_event_get(sh03_events(ch));
                }
            }
            sh03_mutex_give(mutex);
        }
        fw_delay(2000);
    }
}
