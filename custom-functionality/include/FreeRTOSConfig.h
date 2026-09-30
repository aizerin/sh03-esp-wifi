#ifndef FREERTOS_CONFIG_H
#define FREERTOS_CONFIG_H
#include <stdint.h>

/* Recovered: SysTick reload 71999; event priority expression 15-priority;
 * timer task priority 14, stack 512, command queue 10; idle stack 256;
 * heap_4 region 0x5000 bytes; BASEPRI mask 0x50; stack check pattern A5. */
#define configCPU_CLOCK_HZ 72000000UL
#define configTICK_RATE_HZ 1000
#define configMAX_PRIORITIES 15
#define configMINIMAL_STACK_SIZE 256
#define configTOTAL_HEAP_SIZE 20480
#define configMAX_TASK_NAME_LEN 16
#define configUSE_PREEMPTION 1
#define configUSE_TIME_SLICING 1
#define configUSE_PORT_OPTIMISED_TASK_SELECTION 1
#define configUSE_16_BIT_TICKS 0
#define configUSE_IDLE_HOOK 0
#define configUSE_TICK_HOOK 0
#define configUSE_MUTEXES 1
#define configUSE_RECURSIVE_MUTEXES 0
#define configUSE_COUNTING_SEMAPHORES 1
#define configUSE_TASK_NOTIFICATIONS 1
#define configTASK_NOTIFICATION_ARRAY_ENTRIES 1
#define configCHECK_FOR_STACK_OVERFLOW 2
#define configUSE_MALLOC_FAILED_HOOK 0
#define configSUPPORT_DYNAMIC_ALLOCATION 1
#define configSUPPORT_STATIC_ALLOCATION 0
#define configUSE_TIMERS 1
#define configTIMER_TASK_PRIORITY 14
#define configTIMER_QUEUE_LENGTH 10
#define configTIMER_TASK_STACK_DEPTH 512
#define configSTACK_DEPTH_TYPE uint16_t
#define configPRIO_BITS 4
/* The original port did not enable runtime assertions. The build/test checks
 * vector bindings explicitly; omit upstream's assertion-only local variable. */
#define configCHECK_HANDLER_INSTALLATION 0
#define configKERNEL_INTERRUPT_PRIORITY 255
#define configMAX_SYSCALL_INTERRUPT_PRIORITY 0x50
#define INCLUDE_vTaskDelay 1
#define INCLUDE_xTaskDelayUntil 1
#define INCLUDE_vTaskDelete 1
#define INCLUDE_vTaskSuspend 1
#define INCLUDE_eTaskGetState 1
#define INCLUDE_xTaskAbortDelay 1
#define INCLUDE_xTaskGetSchedulerState 1
#define INCLUDE_xTimerPendFunctionCall 1
#define vPortSVCHandler SVC_Handler
#define xPortPendSVHandler PendSV_Handler
#define xPortSysTickHandler SysTick_Handler
#endif
