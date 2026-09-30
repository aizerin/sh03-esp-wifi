#include "sh03_runtime.h"

/* 0x08009e24: IRQ flag is produced by the original EXTI3 handler. Event layout:
 * key byte, untouched zero byte, 16-bit duration in milliseconds. */
void sh03_task_key_scan(void *argument)
{
    (void)argument;
    uint8_t pressed = 0;
    uint32_t event = 0, started = 0;
    SH03_HANDLE(0x20000340) = fw_mutex_create(1);
    for (;;) {
        if (fw_semaphore_take(SH03_HANDLE(0x20000340), 0) == 1) {
            if (SH03_U8(0x200006a4) == 0) {
                fw_enter_critical();
                if (SH03_U8(0x20000259) == 1 && pressed == 1) {
                    uint32_t now = fw_ticks();
                    if (now - started > 1999) {
                        event = (event & 0xffff) | ((now - started) << 16);
                        if ((uint8_t)fw_queue_send(SH03_HANDLE(0x2000036c), &event, 0, 0) != 1) fw_printf("xQueueSend failed\r\n");
                        SH03_U8(0x20000259) = 3;
                        pressed = 0;
                    }
                }
                fw_exit_critical();
            } else {
                if (SH03_U8(0x20000259) == 1) {
                    fw_enter_critical();
                    pressed = 1;
                    started = fw_ticks();
                    fw_exit_critical();
                    event = (event & 0xffffff00u) | fw_touch_read();
                } else if (SH03_U8(0x20000259) == 0 && pressed == 1) {
                    fw_enter_critical();
                    event = (event & 0xffff) | ((fw_ticks() - started) << 16);
                    if ((uint8_t)fw_queue_send(SH03_HANDLE(0x2000036c), &event, 0, 0) != 1) fw_printf("xQueueSend failed\r\n");
                    pressed = 0;
                    fw_exit_critical();
                }
                SH03_U8(0x200006a4) = 0;
            }
            sh03_mutex_give(SH03_HANDLE(0x20000340));
        }
        fw_delay(20);
    }
}
