#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include "semphr.h"
#include "event_groups.h"
#include "timers.h"
#include "sh03_runtime.h"

/* The recovered UI asks about a NULL timer before creating it. The original
 * image happened to have zero bytes at the resulting flash-alias address.
 * With an OTA bootloader there, that read can report a nonexistent timer as
 * active. Never pass nullable handles through to the native FreeRTOS API. */
BaseType_t sh03_timer_is_active(TimerHandle_t timer)
{
    return timer != NULL ? xTimerIsTimerActive(timer) : pdFALSE;
}
BaseType_t sh03_timer_command(TimerHandle_t timer,BaseType_t command,
                             TickType_t value,BaseType_t *woken,TickType_t wait)
{
    if (timer == NULL) return pdFAIL;
    return xTimerGenericCommandFromTask(timer,command,value,woken,wait);
}

/* The recovered wrapper reads the pointed handle, asks for task state (unused
 * result), deletes the task and clears the handle. Valid callers pass a pointer. */
void sh03_delete_task_handle(void **handle)
{
    if (*handle && handle) {
        (void)eTaskGetState(*handle);vTaskDelete(*handle);*handle=0;
    }
}
void vApplicationStackOverflowHook(TaskHandle_t task,char *name)
{
    (void)task;
    sh03_fault_stop_heaters();
    for (;;) fw_printf("Task %s stack overflow\r\n",name);
}
