/* Application state machine recovered from 0x08007b34, countdown 0x080079dc,
 * command producer 0x08001f54, and controller initialization 0x0800ab48. */
#include "sh03_runtime.h"

void sh03_control_initialize_from_message(const Sh03DryingMessage *message)
{
    Sh03ChamberControl *s = &sh03_controls[message->chamber];
    s->running = message->command == SH03_RUN;
    s->countdown_enabled = 0;
    s->chamber_temperature = (float)message->chamber_temperature;
    s->target_temperature = message->target_temperature;
    s->heater_temperature = message->heater_temperature;
    s->cascade_setpoint = message->target_temperature + 5;
    /* Original clears only the low halfword of this reserved word. */
    s->reserved_cc &= 0xffff0000u;
    fw_pid_outer_init(&s->outer, s->target_temperature, s->cascade_setpoint);
    fw_pid_inner_init(&s->inner);
}

void sh03_task_countdown(void *argument)
{
    fw_enter_critical();
    uint8_t diagnostic_position = 0, diagnostic_seconds = 0;
    uint32_t wake = fw_ticks();
    uint8_t chamber = *(uint8_t *)argument;
    void *mutex = fw_mutex_create(1);
    fw_exit_critical();
    Sh03ChamberControl *s = &sh03_controls[chamber];
    for (;;) {
        fw_delay_until(&wake, 1000);
        sh03_elapsed(chamber) += s->countdown_enabled != 0 && s->running != 0;
        sh03_humidity_elapsed(chamber) += fw_mode(chamber) == 1 && s->running != 0;
        if (sh03_diagnostic == 1 && s->countdown_enabled != 0 && ++diagnostic_seconds > 2) {
            diagnostic_position ^= 1;
            fw_semaphore_take(mutex, 5);
            fw_actuator_schedule(chamber, diagnostic_position);
            sh03_mutex_give(mutex);
            diagnostic_seconds = 0;
        }
    }
}

void sh03_task_drying(void *argument)
{
    enum { INITIAL, HUMIDITY_IDLE, HUMIDITY_DRYING, TIMED_DRYING };
    uint8_t phase = INITIAL, outer_divider = 0;
    Sh03DryingMessage *message = argument;
    uint32_t last_control = fw_ticks();
    void *mutex = fw_mutex_create(1);
    /* All original callers pass a valid message; the original null path also
     * dereferences it in control_initialize_from_message. */
    uint8_t chamber = message->chamber, mode = message->mode;
    Sh03ChamberControl *s = &sh03_controls[chamber];
    fw_control_initialize(message);
    void *countdown_task = 0;
    fw_task_create(SH03_FN(0x080079dc, void, void *), "Task_CountDownTimer", 128,
                   &chamber, 3, &countdown_task);
    for (;;) {
        Sh03SensorSnapshot sample;
        fw_sensor_snapshot(&sample, chamber);
        if (fw_queue_receive(sh03_drying_queue(chamber), message, 0) == 1) {
            fw_printf("Task_Drying%d queue recv. p->cmd:%d\r\n", chamber + 1, message->command);
            if (fw_semaphore_take(mutex, 5) == 1) {
                if (message->chamber == chamber) {
                    if (message->command == SH03_STOP) {
                        s->temperature_band = 0;
                        if (sh03_diagnostic == 0) fw_actuators_request(chamber, 0);
                        fw_heater_duty(chamber, 0);
                        s->running = 0;
                        fw_fan_enable(chamber, 0);
                        sh03_reset_elapsed(chamber);
                        s->countdown_enabled = 0;
                        if (mode == SH03_TIMED) fw_event_set(sh03_events(chamber), 2);
                        else if (mode == SH03_HUMIDITY) fw_event_set(sh03_events(chamber), 8);
                        sh03_mutex_give(mutex);
                        fw_task_suspend(0);
                    } else if (message->command == SH03_RUN) {
                        if (mode == SH03_TIMED) {
                            s->temperature_band = fw_temperature_band(chamber, sample.filtered_temperature, message->target_temperature);
                            if (sh03_diagnostic == 0 && s->temperature_band > 1) fw_actuator_schedule(chamber, 1);
                            fw_pid_outer_init(&s->outer, s->target_temperature, s->cascade_setpoint);
                            fw_pid_inner_init(&s->inner);
                            fw_heater_duty(chamber, 50);
                            s->running = 1;
                            fw_fan_enable(chamber, 1);
                            sh03_reset_elapsed(chamber);
                            s->countdown_enabled = 1;
                            fw_event_set(sh03_events(chamber), 1);
                        } else if (mode == SH03_HUMIDITY) phase = INITIAL;
                        fw_printf("RUN\r\n");
                    } else if (message->command == SH03_RESET) {
                        sh03_reset_elapsed(chamber);
                        phase = INITIAL;
                        mode = message->mode;
                        s->target_temperature = message->target_temperature;
                        s->cascade_setpoint = message->target_temperature + 5;
                        fw_pid_outer_init(&s->outer, s->target_temperature, s->cascade_setpoint);
                        fw_pid_inner_init(&s->inner);
                        fw_printf("RESET\r\n");
                    }
                }
                /* The original gives the mutex again after a suspended STOP
                 * resumes. Preserve this observable behavior. */
                sh03_mutex_give(mutex);
            }
        }
        if (phase == INITIAL) {
            if (mode == SH03_HUMIDITY) {
                if ((int32_t)sample.humidity < message->humidity_threshold) {
                    if (sh03_diagnostic == 0) fw_actuators_request(chamber, 0);
                    fw_fan_enable(chamber, 0);
                    fw_heater_duty(chamber, 0);
                    s->running = 0;
                    phase = HUMIDITY_IDLE;
                    fw_event_set(sh03_events(chamber), 2);
                } else {
                    fw_fan_enable(chamber, 1);
                    fw_heater_duty(chamber, 50);
                    s->running = 1;
                    phase = HUMIDITY_DRYING;
                    fw_event_set(sh03_events(chamber), 1);
                }
                sh03_reset_elapsed(chamber);
                s->countdown_enabled = 0;
            } else if (mode == SH03_TIMED) {
                fw_fan_enable(chamber, 1);
                fw_heater_duty(chamber, 50);
                s->running = 1;
                phase = TIMED_DRYING;
                sh03_reset_elapsed(chamber);
                s->countdown_enabled = 1;
                fw_event_set(sh03_events(chamber), 1);
            }
            s->temperature_band = fw_temperature_band(chamber, sample.filtered_temperature, s->target_temperature);
            if (phase != HUMIDITY_IDLE && sh03_diagnostic == 0) fw_actuator_schedule(chamber, s->temperature_band >= 2);
        }
        if (phase == HUMIDITY_IDLE && mode == SH03_HUMIDITY && (int32_t)sample.humidity >= message->humidity_threshold) {
            s->temperature_band = fw_temperature_band(chamber, sample.filtered_temperature, s->target_temperature);
            if (sh03_diagnostic == 0 && s->temperature_band > 1) fw_actuator_schedule(chamber, 1);
            fw_fan_enable(chamber, 1);
            fw_heater_duty(chamber, 50);
            s->running = 1;
            phase = HUMIDITY_DRYING;
            fw_event_set(sh03_events(chamber), 1);
        }
        if (phase == HUMIDITY_DRYING && mode == SH03_HUMIDITY && (int32_t)sample.humidity < message->humidity_threshold) {
            sh03_reset_elapsed(chamber);
            s->countdown_enabled = 1;
            phase = TIMED_DRYING;
        }
        if (phase == TIMED_DRYING && sh03_elapsed(chamber) >= message->duration_seconds) {
            phase = INITIAL;
            if (sh03_diagnostic == 0) fw_actuators_request(chamber, 0);
            fw_heater_duty(chamber, 0);
            s->running = 0;
            fw_fan_enable(chamber, 0);
            sh03_reset_elapsed(chamber);
            s->countdown_enabled = 0;
            if (mode == SH03_TIMED) {
                fw_printf("Chamber%d Time to end work\r\n", chamber);
                fw_event_set(sh03_events(chamber), 8);
                fw_task_suspend(0);
            }
        }
        fw_sensor_snapshot(&sample, chamber);
        if (s->running == 1 && fw_fan_enabled(chamber) == 1 && s->temperature_band != 3) {
            s->temperature_band = fw_temperature_band(chamber, sample.filtered_temperature, message->target_temperature);
            if (sh03_diagnostic == 0 && s->temperature_band == 3) fw_actuator_schedule(chamber, 1);
        }
        uint32_t now = fw_ticks();
        if (s->running == 1 && now - last_control > 200) {
            s->chamber_temperature = sample.temperature;
            s->heater_temperature = (uint8_t)sample.heater_temperature;
            last_control = now;
            if (++outer_divider > 24) {
                outer_divider = 0;
                fw_control_outer(s);
            }
            fw_heater_duty(chamber, fw_control_inner(s));
        }
        if (sh03_requests[chamber].pending == 1 && sh03_requests[chamber].deadline <= fw_ticks()) {
            if (sh03_diagnostic == 0) {
                fw_actuators_set(chamber, sh03_requests[chamber].enabled);
                sh03_requests[chamber].pending = 0;
            } else {
                uint16_t before[4] = {0}, after[4] = {0};
                fw_hall_read(chamber, before);
                fw_actuators_set(chamber, sh03_requests[chamber].enabled);
                sh03_requests[chamber].pending = 0;
                fw_hall_read(chamber, after);
                for (uint8_t actuator = 0; actuator < 2; ++actuator) {
                    unsigned index = chamber * 2u + actuator;
                    int difference = (int)after[index] - (int)before[index];
                    if (difference < 0) difference = -difference;
                    if (difference < 291) fw_latch_fault(chamber, actuator == 0 ? 6 : 7);
                }
            }
        }
        fw_delay(50);
    }
}

static void fill_settings(Sh03DryingMessage *m, uint8_t chamber, const Sh03SensorSnapshot *sample)
{
    m->mode = sh03_mode(chamber);
    m->humidity = (uint8_t)sample->humidity;
    m->humidity_threshold = sh03_ui_field(chamber, 2);
    m->material = sh03_ui_field(chamber, 3);
    m->target_temperature = sh03_ui_field(chamber, 4);
    m->duration_seconds = sh03_mode(chamber) == SH03_HUMIDITY ? 7200 : sh03_ui_duration(chamber);
}

static uint8_t resume_and_send(uint8_t chamber, unsigned send_number)
{
    void *task = sh03_drying_task(chamber);
    if (fw_task_state(task) == 2) fw_abort_delay(task);
    fw_task_resume(task);
    if (fw_semaphore_take(sh03_message_ack(chamber), 0) != 1) {
        fw_printf("Take DryingTaskMsgAck_Sem failed\r\n");
        return 0;
    }
    if (send_number == 2) sh03_messages[chamber].command = SH03_RUN;
    if (fw_queue_send(sh03_drying_queue(chamber), &sh03_messages[chamber], 0, 0) != 1) {
        if (send_number == 2) fw_printf("xQueueSend2 send failed\r\n");
        else fw_printf("xQueueSend1 send failed\r\n");
        return 0;
    }
    return 1;
}

uint8_t sh03_drying_send_command(uint8_t chamber, uint8_t run)
{
    Sh03DryingMessage *m = &sh03_messages[chamber];
    if (run == 1) {
        sh03_ui_field(chamber, 12) = 1;
        Sh03SensorSnapshot sample;
        fw_sensor_snapshot(&sample, chamber);
        if (sh03_drying_task(chamber) == 0) {
            if (sh03_ui_running(chamber) == 0) {
                m->chamber = chamber;
                m->command = SH03_RUN;
                fill_settings(m, chamber, &sample);
                m->chamber_temperature = sh03_ui_field(chamber, 5);
                m->heater_temperature = (uint8_t)sample.heater_temperature;
                void **handle = (void **)(0x200004ec + 4u * chamber);
                if (fw_task_create(SH03_FN(0x08007b34, void, void *), "Task_Drying", 256, m, 6, handle) == 0) {
                    sh03_ui_field(chamber, 14) = 0;
                    fw_printf("Chamber%d Task_Drying create failed\r\n", chamber);
                    return 0;
                }
            }
        } else {
            m->chamber = chamber;
            if (m->mode == sh03_mode(chamber) && m->material == sh03_ui_field(chamber, 3) &&
                m->target_temperature == sh03_ui_field(chamber, 4) && m->duration_seconds == sh03_ui_duration(chamber) &&
                (sh03_mode(chamber) != SH03_HUMIDITY || m->humidity_threshold == sh03_ui_field(chamber, 2))) {
                if (sh03_ui_running(chamber) != 0) return 0;
                return resume_and_send(chamber, 2);
            }
            m->command = SH03_RESET;
            fill_settings(m, chamber, &sample);
            return resume_and_send(chamber, 1);
        }
    } else if (sh03_ui_running(chamber) == 1 && sh03_drying_task(chamber) != 0) {
        uint8_t state = (uint8_t)fw_task_state(sh03_drying_task(chamber));
        if (state == 3 || state == 4 || state == 5) return 0;
        if (fw_semaphore_take(sh03_message_ack(chamber), 0) != 1) {
            fw_printf("Take DryingTaskMsgAck_Sem failed\r\n");
            return 0;
        }
        m->chamber = chamber;
        m->command = SH03_STOP;
        /* Original does not inspect the queue-send result on the stop path. */
        fw_queue_send(sh03_drying_queue(chamber), m, 0, 0);
    }
    return 1;
}
