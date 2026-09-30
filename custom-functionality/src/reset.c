#include "sh03_runtime.h"
#include <stddef.h>
#include "sh03_link.h"

extern uint32_t _stack_top,_data_start,_data_end,_data_load,_bss_start,_bss_end;
extern const uint32_t sh03_initial_state[170];
extern void sh03_system_init(void);
extern int sh03_main(void);
extern void SVC_Handler(void),PendSV_Handler(void),SysTick_Handler(void);
extern void sh03_touch_irq(void),sh03_dma1_channel4_irq(void),sh03_dma1_channel5_irq(void),sh03_dma2_channel4_5_irq(void);
void Reset_Handler(void);
static void Default_Handler(void) { sh03_fault_stop_heaters(); for (;;) { } }
static void NMI_Handler(void) { sh03_fault_stop_heaters(); for (;;) fw_printf("NMI_Handler\r\n"); }
static void HardFault_Handler(void) { sh03_fault_stop_heaters(); for (;;) fw_printf("HardFault_Handler\r\n"); }
static void MemManage_Handler(void) { sh03_fault_stop_heaters(); for (;;) fw_printf("MemManage_Handler\r\n"); }
static void BusFault_Handler(void) { sh03_fault_stop_heaters(); for (;;) fw_printf("BusFault_Handler\r\n"); }
static void UsageFault_Handler(void) { sh03_fault_stop_heaters(); for (;;) fw_printf("UsageFault_Handler\r\n"); }
static void DebugMon_Handler(void) { sh03_fault_stop_heaters(); for (;;) fw_printf("DebugMon_Handler\r\n"); }

/* GNU range initialization followed by named overrides matches the 76-vector
 * high-density STM32F1 table in the dump. Cortex-M3 exception entry is hardware. */
__attribute__((section(".vectors"),used))
const uintptr_t sh03_vectors[76]={
    [0]=(uintptr_t)&_stack_top,[1]=(uintptr_t)Reset_Handler,[2]=(uintptr_t)NMI_Handler,
    [3]=(uintptr_t)HardFault_Handler,[4]=(uintptr_t)MemManage_Handler,[5]=(uintptr_t)BusFault_Handler,
    [6]=(uintptr_t)UsageFault_Handler,[11]=(uintptr_t)SVC_Handler,[12]=(uintptr_t)DebugMon_Handler,
    [14]=(uintptr_t)PendSV_Handler,[15]=(uintptr_t)SysTick_Handler,
    [16 ... 75]=(uintptr_t)Default_Handler,
    [16+9]=(uintptr_t)sh03_touch_irq,[16+14]=(uintptr_t)sh03_dma1_channel4_irq,
    [16+15]=(uintptr_t)sh03_dma1_channel5_irq,[16+59]=(uintptr_t)sh03_dma2_channel4_5_irq,
    [16+52]=(uintptr_t)sh03_uart4_irq,
};
void Reset_Handler(void)
{
    sh03_system_init();
    uint32_t *source=&_data_load;
    for (uint32_t *p=&_data_start;p<&_data_end;) *p++=*source++;
    for (uint32_t *p=&_bss_start;p<&_bss_end;) *p++=0;
    for (unsigned i=0;i<0x700/4;++i) ((volatile uint32_t *)0x20000000)[i]=0;
    for (unsigned i=0;i<170;++i) ((volatile uint32_t *)0x20000000)[i]=sh03_initial_state[i];
    (void)sh03_main();
    for (;;) { }
}
