#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include "semphr.h"
#include "event_groups.h"
#include "timers.h"
#include "sh03_runtime.h"

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
    for (;;) fw_printf("Task %s stack overflow\r\n",name);
}
