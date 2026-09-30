/* Application startup. Original addresses are provenance keys: the standalone
 * build resolves them to linked C/FreeRTOS symbols; c-variant uses trampolines.
 * reset.c supplies reset, RAM initialization and vectors in the standalone build. */
#include "sh03_ui.h"

void sh03_task_initialize(void *argument)
{
    (void)argument;
    fw_enter_critical();
    SH03_HANDLE(0x20000334) = fw_mutex_create(1);
    SH03_HANDLE(0x2000032c) = SH03_FN(0x0800df70, void *, void)();
    SH03_HANDLE(0x20000330) = SH03_FN(0x0800df70, void *, void)();
    SH03_HANDLE(0x2000036c) = fw_queue_create(4, 4, 0);
    sh03_drying_queue(0) = fw_queue_create(1, 16, 0);
    sh03_drying_queue(1) = fw_queue_create(1, 16, 0);
    fw_bus_initialize((void *)0x20000000);
    if (fw_task_create(SH03_FN(0x08009e24, void, void *), "Task_KeyScan", 192, 0, 6, (void **)0x200004e0) == 0) fw_printf("Task_KeyScan create failed\r\n");
    if (fw_task_create(SH03_FN(0x08009fb8, void, void *), "Task_SensorDataUpdate", 256, 0, 4, (void **)0x200004e4) == 0) fw_printf("Task_SensorDataUpdate create failed\r\n");
    if (fw_task_create(SH03_FN(0x080085bc, void, void *), "Task_InteractiveProcess", 256, 0, 5, (void **)0x200004e8) == 0) fw_printf("Task_InteractiveProcess create failed\r\n");
    if (fw_task_create(SH03_FN(0x0800a4c4, void, void *), "Task_SystemMonitor", 256, 0, 3, (void **)0x200004f4) == 0) fw_printf("Task_SystemMonitor create failed\r\n");
    fw_exit_critical();
    fw_task_delete(0);
}

int sh03_main(void)
{
    SH03_FN(0x0800b48c, void, uint32_t)(0x300); /* Original AIRCR priority setup argument. */
    SH03_FN(0x0800b17c, void, void)(); /* Debug UART. */
    SH03_FN(0x0800ac0c, void, void)(); /* TIM6 delay clock. */
    SH03_FN(0x0800ac44, void, uint16_t)(2);
    SH03_FN(0x080034a4, void, void)(); /* Fan PWM/tachometers. */
    SH03_FN(0x08004858, void, void)(); /* Hall ADC. */
    SH03_FN(0x08005290, void, void)(); /* NTC ADC. */
    SH03_FN(0x080049f8, void, void)(); /* Heater PWM. */
    SH03_FN(0x08004690, void, void)(); /* Actuator GPIO. */
    SH03_FN(0x0800b414, void, void)(); /* Touch IRQ. */
    SH03_FN(0x080019e0, void, void)(); /* Alarm GPIO. */
    SH03_FN(0x080066e0, void, void)(); /* Display interface. */
    SH03_FN(0x08001a94, void, void)(); /* Clear display. */
    SH03_FN(0x0800ac44, void, uint16_t)(400);
    SH03_FN(0x08001980, void, void)(); /* Common backlight GPIO, PB1. */
    SH03_FN(0x08002580, void, uint8_t)(0); /* PB1 HIGH: backlights on. */
    SH03_FN(0x08002d6c, void, void)(); /* Display boot animation. */
    fw_printf("Frimware Version: %s\r\n", "V3.5.1");
    fw_printf("FreeRTOS Version: %s\r\n", "V11.1.0");
    if (fw_task_create(SH03_FN(0x08004ea4, void, void *), "Init_Task", 256, 0, 4, SH03_HANDLE(0x200004dc)) == 1) {
        fw_printf("Init_Task create successed\r\n");
        SH03_FN(0x0800d9f8, void, void)();
        for (;;) { }
    }
    fw_printf("Init_Task create failed\r\n");
    return -1;
}
