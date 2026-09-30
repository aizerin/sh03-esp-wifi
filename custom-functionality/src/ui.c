/* User interaction recovered from 0x080085bc and the setting timer callbacks.
 * Key numbers are the raw XW05A codes, kept distinct from their panel labels. */
#include "sh03_ui.h"
#ifdef SH03_CUSTOM_UART
#include "sh03_link.h"
#include "sh03_display.h"
#endif

static uint8_t refresh_chamber(uint8_t chamber) { return chamber == 0 ? 0x11 : 0x21; }

void sh03_ui_initialize(Sh03Ui *state, uint8_t chambers)
{
    state->chamber = 0;
    state->blink = 1;
    for (uint8_t ch = 0; ch < chambers; ++ch) {
        Sh03ChamberUi *s = &state->chambers[ch];
        s->fault_shown = s->selected = s->humidity_icon = s->running_icon = s->humidity = 0;
        s->humidity_threshold = 20;
        s->material = 0;
        s->target_temperature = 50;
        s->temperature = 0;
        s->duration_seconds = 28800;
        s->setting_item = 1;
    }
}

void sh03_blink_callback(void *timer)
{
    (void)timer;
    if (fw_semaphore_take(sh03_ui_mutex, 2) == 1) {
        sh03_ui.blink ^= 1;
        sh03_mutex_give(sh03_ui_mutex);
    }
}

void sh03_clear_setting_callback(void *timer)
{
    (void)timer; /* One-shot timer is inactive on entry; retain it for reuse. */
    int32_t taken = fw_semaphore_take(sh03_ui_mutex, 2);
    uint8_t ch = sh03_ui.chamber;
    if (taken == 1) {
        sh03_setting = 0;
        if (ch < 2) {
            Sh03ChamberUi *s = &sh03_ui.chambers[ch];
            Sh03DryingMessage *m = &sh03_messages[ch];
            if (sh03_mode(ch) == SH03_TIMED) {
                if (sh03_ui_running(ch) == 0) {
                    s->selected = s->running_icon = 0;
                    s->target_temperature = sh03_materials[s->material].temperature;
                    s->duration_seconds = sh03_materials[s->material].duration_seconds;
                } else if (sh03_ui_running(ch) == 1) {
                    fw_display_setting(s, ch, 1);
                    if (m->material != s->material || m->target_temperature != s->target_temperature || m->duration_seconds != s->duration_seconds) {
                        m->command = SH03_RESET;
                        m->mode = SH03_TIMED;
                        m->material = s->material;
                        m->target_temperature = s->target_temperature;
                        m->duration_seconds = s->duration_seconds;
                        fw_queue_send(sh03_drying_queue(ch), m, 10, 0);
                        fw_task_resume(sh03_drying_task(ch));
                    }
                }
            } else if (sh03_mode(ch) == SH03_HUMIDITY) {
                fw_drying_command(ch, 1);
                if (sh03_ui_running(ch) == 1) fw_display_setting(s, ch, 1);
            }
            fw_notify(SH03_HANDLE(0x200004e8), 0, refresh_chamber(ch), 3, 0);
        }
        sh03_mutex_give(sh03_ui_mutex);
    }
}

void sh03_clear_ui_fault(uint8_t ch)
{
    Sh03ChamberUi *s = &sh03_ui.chambers[ch];
    if (s->fault_shown == 1) {
        uint8_t fault = SH03_U8(0x2000002c + ch * 2u);
        if (fault == 6 || fault == 7) fw_display_material(ch, s->material, 0);
        else {
            fw_delete_task_handle((void **)0x200004f8);
            SH03_U32(0x422181b0) = 0;
        }
        s->fault_shown = 0;
        SH03_U8(0x2000002c + ch * 2u) = 10;
    }
}

void sh03_copy_settings(uint8_t source, uint8_t destination)
{
    Sh03ChamberUi *from = &sh03_ui.chambers[source], *to = &sh03_ui.chambers[destination];
    if (sh03_mode(source) != sh03_mode(destination)) to->setting_item = sh03_mode(source) != 1;
    sh03_mode(destination) = sh03_mode(source);
    to->humidity_icon = from->humidity_icon;
    to->humidity_threshold = from->humidity_threshold;
    to->material = from->material;
    to->target_temperature = from->target_temperature;
    to->duration_seconds = from->duration_seconds;
}

void sh03_display_setting(const Sh03ChamberUi *s, uint8_t chamber, uint8_t visible)
{
    switch (s->setting_item) {
    case 0: fw_display_humidity(chamber, s->humidity, visible); break;
    case 1: fw_display_material(chamber, s->material, visible); break;
    case 2: fw_display_temperature(chamber, s->target_temperature, visible); break;
    case 3: fw_display_time(chamber, (uint8_t)(s->duration_seconds / 3600), (uint8_t)((s->duration_seconds / 60) % 60), visible); break;
    }
}

static void stop_setting_timer(void *timer)
{
    if (timer != 0 && fw_timer_active(timer) != 0 && fw_timer_command(timer, 3, 0, 0, 0) == 0) fw_printf("xTimerStop failed\r\n");
}

static void restart_setting_timer(void **timer, uint8_t key)
{
    uint32_t period = key == 1 ? 10000 : 10005;
    if (*timer == 0) {
        *timer = fw_timer_create(key == 1 ? "ClearSettingFlagTimer" : "Clear_SettingFlag_CB",
                                 period, 0, (void *)2, SH03_FN(0x08001ad8, void, void *));
        if (*timer != 0) fw_timer_command(*timer, 1, fw_ticks(), 0, 1);
    } else if (fw_timer_active(*timer) == 1) {
        fw_timer_command(*timer, 2, fw_ticks(), 0, 1);
    } else {
        /* CHANGE_PERIOD also starts an inactive timer. Preserve the original
         * key-specific timeout without deleting a handle still owned by UI. */
        fw_timer_command(*timer, 4, period, 0, 1);
    }
}

static uint8_t running_refresh(uint8_t chamber)
{
    return sh03_ui_running(0) == 1 && sh03_ui_running(1) == 1 ? 0x31 : refresh_chamber(chamber);
}

static void restore_settings(uint8_t select_chamber)
{
    for (uint8_t ch = 0; ch < 2; ++ch) {
        Sh03ChamberUi *s = &sh03_ui.chambers[ch];
        Sh03DryingMessage *m = &sh03_messages[ch];
        uint8_t material = s->material;
        if (sh03_mode(ch) == SH03_HUMIDITY) {
            if (select_chamber) {
                if (ch == sh03_ui.chamber) s->humidity = s->humidity_threshold;
            } else s->humidity = m->humidity_threshold;
        }
        uint8_t running = sh03_ui_running(ch) == 1;
        s->material = running ? m->material : material;
        s->target_temperature = running ? m->target_temperature : sh03_materials[material].temperature;
        s->duration_seconds = running ? m->duration_seconds : sh03_materials[material].duration_seconds;
        if (select_chamber) s->humidity_threshold = running ? m->humidity_threshold : s->humidity_threshold;
    }
}

static void change_setting(uint8_t ch, uint8_t increase)
{
    Sh03ChamberUi *s = &sh03_ui.chambers[ch];
    switch (s->setting_item) {
    case 0:
        s->humidity_threshold += increase ? 5 : -5;
        if (increase && s->humidity_threshold >= 45) s->humidity_threshold = 20;
        if (!increase && s->humidity_threshold < 16) s->humidity_threshold = 40;
        s->humidity = s->humidity_threshold;
        break;
    case 1:
        s->material = (s->material + (increase ? 1 : 9)) % 10;
        s->target_temperature = sh03_materials[s->material].temperature;
        s->duration_seconds = sh03_mode(ch) == SH03_HUMIDITY ? 7200 : sh03_materials[s->material].duration_seconds;
        break;
    case 2:
        s->target_temperature += increase ? 5 : -5;
        if (increase) s->target_temperature = (s->target_temperature / 90) * 45 + s->target_temperature % 90;
        else if (s->target_temperature < 41) s->target_temperature = 85;
        break;
    case 3:
        if (increase) {
            s->duration_seconds += 7200;
            s->duration_seconds = (s->duration_seconds / 360000) * 7200 + s->duration_seconds % 360000;
        } else {
            s->duration_seconds -= 7200;
            if (s->duration_seconds == 0) s->duration_seconds = 352800;
        }
        break;
    }
}

void sh03_task_interactive(void *argument)
{
    (void)argument;
    fw_ui_initialize(&sh03_ui, 2);
    uint8_t redraw = 0;
    void *setting_timer = 0, *blink_timer = 0;
    sh03_ui_mutex = fw_mutex_create(1);
    sh03_message_ack(0) = fw_queue_create(1, 0, 3);
    sh03_message_ack(1) = fw_queue_create(1, 0, 3);
    sh03_messages[0] = (Sh03DryingMessage){0};
    sh03_messages[1] = (Sh03DryingMessage){0};
    if (fw_timer_active(blink_timer) != 1) {
        blink_timer = fw_timer_create("FlickerSettingItemTimer", 500, 1, (void *)3, SH03_FN(0x08003820, void, void *));
        fw_timer_command(blink_timer, 1, fw_ticks(), 0, 1);
    }
    #ifdef SH03_CUSTOM_UART
    sh03_link_init();
    #endif
    for (;;) {
        if (fw_semaphore_take(sh03_ui_mutex, 2) == 1) {
            for (uint8_t ch = 0; ch < 2; ++ch) {
                Sh03ChamberUi *s = &sh03_ui.chambers[ch];
                Sh03SensorSnapshot sample;
                fw_sensor_snapshot(&sample, ch);
                s->temperature = (uint8_t)sample.filtered_temperature;
                if (sh03_mode(ch) == SH03_TIMED) {
                    s->humidity = (uint8_t)sample.humidity;
                    if ((sh03_setting == 0 || ch != sh03_ui.chamber) && sh03_ui_running(ch) == 1) s->duration_seconds = sh03_messages[ch].duration_seconds - fw_elapsed(ch);
                } else if (sh03_mode(ch) == SH03_HUMIDITY && (sh03_setting == 0 || ch != sh03_ui.chamber)) {
                    s->humidity = (uint8_t)sample.humidity;
                    if (sh03_ui_running(ch) == 1) s->duration_seconds = sh03_messages[ch].duration_seconds - fw_elapsed(ch);
                }
                /* FreeRTOS returns the entire event group. Humidity mode can
                 * retain 0x80 alongside RUN/PAUSE/END, so test only our bit. */
                if (fw_event_wait(sh03_events(ch), 1, 1, 0, 0) & 1) {
                    fw_enter_critical();
                    s->running_icon = 1;
                    sh03_ui_running(ch) = 1;
                    stop_setting_timer(setting_timer);
                    redraw = running_refresh(ch);
                    sh03_setting = 0;
                    if (ch == 0 || sh03_ui_running(0) == 1) fw_display_setting(&sh03_ui.chambers[0], 0, 1);
                    if (ch == 1 || sh03_ui_running(1) == 1) fw_display_setting(&sh03_ui.chambers[1], 1, 1);
                    fw_exit_critical();
                    sh03_mutex_give(sh03_message_ack(ch));
                    fw_printf("Chamber%d EV_WORK_RUN_Bit\r\n", ch + 1);
                } else if (fw_event_wait(sh03_events(ch), 2, 1, 0, 0) & 2) {
                    fw_enter_critical();
                    if (sh03_mode(ch) == SH03_TIMED) {
                        fw_event_get(sh03_events(ch));
                        s->running_icon = 0;
                        sh03_ui_running(ch) = 0;
                        stop_setting_timer(setting_timer);
                        s->selected = 0;
                    } else if (sh03_mode(ch) == SH03_HUMIDITY) {
                        fw_event_set(sh03_events(ch), 0x80);
                        s->running_icon = 0;
                        sh03_ui_running(ch) = 1;
                        stop_setting_timer(setting_timer);
                        redraw = running_refresh(ch);
                    }
                    sh03_setting = 0;
                    fw_exit_critical();
                    sh03_mutex_give(sh03_message_ack(ch));
                    fw_printf("EV_WORK_PAUSE_Bit\r\n");
                } else if (fw_event_wait(sh03_events(ch), 8, 1, 0, 0) & 8) {
                    fw_enter_critical();
                    if (sh03_messages[ch].mode == SH03_TIMED) {
                        if (sh03_setting == 0) s->selected = 0;
                    } else {
                        sh03_mutex_give(sh03_message_ack(ch));
                        s->humidity_icon = 0;
                    }
                    s->running_icon = 0;
                    sh03_ui_running(ch) = 0;
                    fw_exit_critical();
                    fw_printf("EV_WORK_END_Bit\r\n");
                } else {
                    if (SH03_U8(0x2000002d + ch * 2u) == 1) {
                        s->fault_shown = 1;
                        uint8_t fault = SH03_U8(0x2000002c + ch * 2u);
                        if (fault == 6 || fault == 7) {
                            if (sh03_ui_running(ch) == 0) {
                                sh03_setting = 0;
                                s->selected = s->running_icon = 0;
                            } else fw_display_material(ch, s->material, 0);
                        } else {
                            s->selected = s->running_icon = 0;
                            fw_drying_command(ch, 0);
                            if (SH03_HANDLE(0x200004f8) == 0 && (uint8_t)fw_task_create(SH03_FN(0x0800a464, void, void *), "Task_SystemErrAlarm", 128, 0, 6, (void **)0x200004f8) == 0) fw_printf("Task_SystemErrAlarm create failed\r\n");
                            sh03_ui_running(ch) = 0;
                        }
                        SH03_U8(0x2000002d + ch * 2u) = 0;
                    }
                    sh03_mutex_give(sh03_ui_mutex);
                    if ((fw_event_get(sh03_events(ch)) & 0x10) && fw_semaphore_take(SH03_HANDLE(0x20000334), 5) == 1) {
                        /* 0x08008dd2..0x08008de2: the pseudocode export omitted
                         * the low pulses as dead stores to bit-band aliases. */
                        SH03_U32(0x422181a8) = 0;
                        SH03_U32(0x422181ac) = 0;
                        SH03_U32(0x422181a8) = 1;
                        SH03_U32(0x422181ac) = 1;
                        fw_bus_initialize((void *)0x20000000);
                        fw_event_get(sh03_events(ch));
                        sh03_mutex_give(SH03_HANDLE(0x20000334));
                    }
                    if (ch == 0) fw_semaphore_take(sh03_ui_mutex, 0xffffffff);
                }
            }
            sh03_mutex_give(sh03_ui_mutex);
        } else fw_printf("xSemaphoreTake failed\r\n");

        uint32_t key_event;
        if (fw_queue_receive(SH03_HANDLE(0x2000036c), &key_event, 0) == 1 && fw_semaphore_take(sh03_ui_mutex, 5) == 1) {
            #ifdef SH03_CUSTOM_UART
            if (!sh03_display_enabled()) {
                sh03_display_set_enabled(1);
                /* Consume the whole short/long touch event. Key zero has no
                 * action, so waking cannot start/stop or edit the dryer. */
                key_event=0;
            }
            #endif
            uint8_t key = key_event, ch = sh03_ui.chamber;
            uint16_t duration = key_event >> 16;
            if (key == 1) {
                restart_setting_timer(&setting_timer, key);
                if (duration < 2000) {
                    if (sh03_setting == 0) {
                        sh03_setting = 1;
                        sh03_ui.blink = 1;
                        uint8_t other = (ch + 1) & 1;
                        if (sh03_ui_running(ch) == 1 && sh03_ui_running(other) != 1) sh03_ui.chamber = other;
                        sh03_ui.chambers[sh03_ui.chamber].selected = 1;
                    } else {
                        sh03_setting = 1;
                        if (sh03_mode(ch) == SH03_HUMIDITY) redraw = refresh_chamber(ch);
                        else if (sh03_ui_running(ch) == 0 || sh03_messages[ch].command == SH03_STOP) sh03_ui.chambers[ch].selected = 0;
                        else redraw = refresh_chamber(ch);
                        sh03_ui.chamber = (ch + 1) & 1;
                        sh03_ui.chambers[sh03_ui.chamber].selected = 1;
                    }
                    restore_settings(1);
                    fw_clear_ui_fault(sh03_ui.chamber);
                } else if (sh03_setting != 0) {
                    fw_copy_settings(ch, (ch + 1) & 1);
                    for (uint8_t c = 0; c < 2; ++c) {
                        fw_drying_command(c, 1);
                        if (c == 0) { sh03_mutex_give(sh03_ui_mutex); fw_delay(100); fw_semaphore_take(sh03_ui_mutex, 0xffffffff); }
                    }
                }
            } else if (key == 2) {
                restart_setting_timer(&setting_timer, key);
                Sh03ChamberUi *s = &sh03_ui.chambers[ch];
                if (duration < 2000) {
                    if (sh03_setting == 2) {
                        ++s->setting_item;
                        if (sh03_mode(ch) == SH03_HUMIDITY) s->setting_item %= 3;
                        else { s->setting_item &= 3; if (s->setting_item == 0) s->setting_item = 1; }
                    } else {
                        if (sh03_setting == 1) redraw = refresh_chamber(ch);
                        else if (sh03_setting == 0 && sh03_ui_running(ch) != 1 && sh03_ui_running((ch + 1) & 1) == 1) ch = (ch + 1) & 1;
                        sh03_setting = 2;
                        sh03_ui.chamber = ch;
                        sh03_ui.chambers[ch].selected = 1;
                        sh03_ui.blink = 1;
                        restore_settings(0);
                    }
                } else if (sh03_setting == 1) {
                    sh03_mode(ch) ^= 1;
                    s->target_temperature = sh03_materials[s->material].temperature;
                    if (sh03_mode(ch) == SH03_HUMIDITY) {
                        s->setting_item = 0;
                        s->humidity = s->humidity_threshold;
                        s->humidity_icon = 1;
                        s->running_icon = 0;
                        fw_drying_command(ch, 1);
                    } else {
                        s->setting_item = 1;
                        s->duration_seconds = sh03_materials[s->material].duration_seconds;
                        s->humidity_icon = s->running_icon = 0;
                        fw_drying_command(ch, 0);
                    }
                }
                fw_clear_ui_fault(sh03_ui.chamber);
            } else if (key == 3 || key == 4) {
                if (sh03_setting == 2) {
                    if (setting_timer != 0) fw_timer_command(setting_timer, 2, fw_ticks(), 0, 1);
                    change_setting(ch, key == 3);
                }
            } else if (key == 5) {
                if (sh03_setting == 1 && sh03_mode(ch) == SH03_TIMED) {
                    uint8_t run = sh03_ui_running(ch) == 0;
                    if (!run) fw_set_diagnostic(0);
                    fw_drying_command(ch, run);
                } else if (sh03_setting == 2 && fw_drying_command(ch, 1) == 0) {
                    stop_setting_timer(setting_timer);
                    redraw = ch == 0 ? 0x10 : 0x20;
                    sh03_setting = 0;
                }
            } else if (key == 6 && duration > 1999) {
                uint8_t run = fw_get_diagnostic() == 0;
                fw_set_diagnostic(run);
                for (uint8_t c = 0; c < 2; ++c) {
                    fw_drying_command(c, run);
                    if (c == 0) { sh03_mutex_give(sh03_ui_mutex); fw_delay(100); fw_semaphore_take(sh03_ui_mutex, 0xffffffff); }
                    sh03_ui.chambers[c].selected = run;
                }
            }
            sh03_mutex_give(sh03_ui_mutex);
        }
        #ifdef SH03_CUSTOM_UART
        /* Remote commands update chamber state. Let the normal renderer use
         * its visibility/cache; forcing 0x31 lights even hidden chambers. */
        sh03_link_poll();
        #endif
        uint32_t notification = 0;
        if (redraw == 0 && fw_notify_wait(0, 0, 0xffffffff, &notification, 0) == 1) redraw = (uint8_t)notification;
        fw_display_render((void *)0x20000018, &sh03_ui, redraw);
        redraw = 0;
        fw_delay(100);
    }
}
