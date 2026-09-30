#include "sh03_ui.h"
#include "sh03_protocol.h"
#include "sh03_link.h"
#include "sh03_display.h"

uint8_t sh03_remote_command(uint8_t ch, uint8_t op, uint32_t value)
{
    if (ch>1) return SH03_RANGE;
    if (op<SH03_OP_RUN || op>SH03_OP_DISPLAY_TIMEOUT) return SH03_UNSUPPORTED;
    if ((op==SH03_OP_RUN && value>1) ||
        (op==SH03_OP_TEMPERATURE && (value<45 || value>85)) ||
        (op==SH03_OP_DURATION && (value<7200 || value>352800 || value%7200)) ||
        (op==SH03_OP_MODE && value>1) ||
        (op==SH03_OP_HUMIDITY && (value<20 || value>40 || value%5)) ||
        (op==SH03_OP_MATERIAL && value>9) ||
        (op==SH03_OP_DISPLAY && value>1)) return SH03_RANGE;
    if (fw_ticks()<2000) return SH03_NOT_READY;
    /* The two LCDs share one output switch. It is independent of drying,
     * local editing, diagnostic mode and fault handling. No control state,
     * task, timer or sensor setting is changed by this operation. */
    if (op==SH03_OP_DISPLAY) {
        sh03_display_set_enabled((uint8_t)value);
        return SH03_OK;
    }
    if (op==SH03_OP_DISPLAY_TIMEOUT) {
        /* Legacy ESPHome clients may still expose the removed timer. Accept
         * disabled only; a stale client cannot re-enable automatic blanking. */
        return value==0?SH03_OK:SH03_UNSUPPORTED;
    }
    Sh03ChamberUi *s=&sh03_ui.chambers[ch];
    void *task=sh03_drying_task(ch);
    uint8_t enabled=sh03_ui_running(ch);
    int suspended=task && fw_task_state(task)==3;
    /* STOP is also accepted during local editing and with a latched fault.
     * Remote control never clears faults or drives heater PWM directly. */
    if (op==SH03_OP_RUN && value==0) {
        if (!task || suspended) return SH03_OK;
        if (fw_semaphore_take(sh03_message_ack(ch),0)!=1) return SH03_BUSY;
        Sh03DryingMessage message=sh03_messages[ch];
        message.chamber=ch; message.command=SH03_STOP;
        if (fw_queue_send(sh03_drying_queue(ch),&message,0,0)!=1) {
            sh03_mutex_give(sh03_message_ack(ch)); return SH03_BUSY;
        }
        sh03_diagnostic=0;
        return SH03_OK;
    }
    if (sh03_setting || sh03_diagnostic) return SH03_BUSY;
    if (fw_fault(ch)!=10) return SH03_FAULT;
    if (op==SH03_OP_RUN) {
        if (enabled) return SH03_OK;
        if (task && !suspended) return SH03_BUSY;
        Sh03SensorSnapshot sample; fw_sensor_snapshot(&sample,ch);
        if (!sample.humidity || sample.humidity>100 || sample.heater_temperature<-18 ||
            sample.heater_temperature>=111) return SH03_FAULT;
        if (!task) {
            if (!sh03_drying_send_command(ch,1)) return SH03_BUSY;
            s->humidity_icon=sh03_mode(ch)==SH03_HUMIDITY;
            return SH03_OK;
        }
        if (fw_semaphore_take(sh03_message_ack(ch),0)!=1) return SH03_BUSY;
        Sh03DryingMessage message={0};
        message.chamber=ch; message.command=SH03_RESET; message.mode=sh03_mode(ch);
        message.material=s->material; message.target_temperature=s->target_temperature;
        message.humidity_threshold=s->humidity_threshold; message.humidity=(uint8_t)sample.humidity;
        message.chamber_temperature=(uint8_t)sample.filtered_temperature;
        message.heater_temperature=(uint8_t)sample.heater_temperature;
        message.duration_seconds=message.mode==SH03_HUMIDITY?7200:s->duration_seconds;
        if (fw_queue_send(sh03_drying_queue(ch),&message,0,0)!=1) {
            sh03_mutex_give(sh03_message_ack(ch)); return SH03_BUSY;
        }
        fw_task_resume(task);
        s->selected=1;
        s->humidity_icon=sh03_mode(ch)==SH03_HUMIDITY;
        return SH03_OK;
    }
    /* Settings change only while stopped, avoiding races with the control task
     * and unexpected restarts when a Home Assistant slider is dragged. */
    if (enabled || (task && !suspended)) return SH03_BUSY;
    switch (op) {
    case SH03_OP_TEMPERATURE: s->target_temperature=(uint8_t)value; break;
    case SH03_OP_DURATION:
        if (sh03_mode(ch)==SH03_HUMIDITY) return SH03_UNSUPPORTED;
        s->duration_seconds=value; break;
    case SH03_OP_MODE:
        sh03_mode(ch)=(uint8_t)value; s->humidity_icon=(uint8_t)value;
        s->setting_item=value?0:1;
        s->duration_seconds=value?7200:sh03_materials[s->material].duration_seconds;
        break;
    case SH03_OP_HUMIDITY: s->humidity_threshold=(uint8_t)value; break;
    case SH03_OP_MATERIAL:
        s->material=(uint8_t)value; s->target_temperature=sh03_materials[value].temperature;
        s->duration_seconds=sh03_mode(ch)==SH03_HUMIDITY?7200:sh03_materials[value].duration_seconds;
        break;
    default: return SH03_UNSUPPORTED;
    }
    /* selected is the renderer's visibility flag, not the local edit mode.
     * Keep remotely prepared settings visible as sensor values change, without
     * starting the panel's blink/timeout workflow or lighting the other side. */
    s->selected=1;
    return SH03_OK;
}
