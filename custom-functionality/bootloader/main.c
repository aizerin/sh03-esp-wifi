/* STM32F1 HD backend. PM0075: page erase and half-word programming only.
 * No option bytes, mass erase, interrupts, heap, or RTOS in the bootloader. */
#include "sh03_update.h"
#define R32(a) (*(volatile uint32_t *)(uintptr_t)(a))
#define R16(a) (*(volatile uint16_t *)(uintptr_t)(a))
#define FLASH 0x40022000u
#define UART 0x40004c00u
#define TIM2 0x40000000u
extern uint32_t _stack_top,_data_start,_data_end,_data_load,_bss_start,_bss_end;
void Boot_Reset(void);
static void fault(void) { for (;;) { } }
void *memset(void *dest,int value,size_t size)
{
    uint8_t *p=dest; while (size--) *p++=(uint8_t)value; return dest;
}
__attribute__((section(".vectors"),used))
const uintptr_t boot_vectors[76]={
    (uintptr_t)&_stack_top,(uintptr_t)Boot_Reset,[2 ... 75]=(uintptr_t)fault
};

const uint8_t *sh03_flash_at(uint32_t address) { return (const uint8_t *)(uintptr_t)address; }
static uint16_t ticks(void) { return R16(TIM2+0x24); }
static void watchdog(void) { R32(0x40003000)=0xaaaa; }
static int writable(uint32_t address) { return address>=SH03_APP_BASE && address<SH03_ACTIVE_BASE+SH03_PAGE_SIZE; }
static int flash_wait(void)
{
    uint16_t start=ticks();
    while (R32(FLASH+0x0c)&1) { watchdog(); if ((uint16_t)(ticks()-start)>500) return 0; }
    return !(R32(FLASH+0x0c)&0x14);
}
static int unlock(void)
{
    if (!flash_wait()) return 0;
    if (R32(FLASH+0x10)&0x80) { R32(FLASH+4)=0x45670123; R32(FLASH+4)=0xcdef89ab; }
    R32(FLASH+0x0c)=0x34;
    return !(R32(FLASH+0x10)&0x80);
}
int sh03_flash_erase(uint32_t address)
{
    if (!writable(address) || (address&(SH03_PAGE_SIZE-1)) || !unlock()) return 0;
    R32(FLASH+0x10)=2; R32(FLASH+0x14)=address; R32(FLASH+0x10)=0x42;
    int ok=flash_wait(); R32(FLASH+0x10)=0x80;
    for (unsigned i=0;ok && i<SH03_PAGE_SIZE;i+=4) if (R32(address+i)!=0xffffffffu) ok=0;
    watchdog(); return ok;
}
int sh03_flash_program(uint32_t address,uint16_t value)
{
    if (!writable(address) || (address&1)) return 0;
    if (R16(address)==value) return 1;
    if (R16(address)!=0xffff || !unlock()) return 0;
    R32(FLASH+0x10)=1; R16(address)=value;
    int ok=flash_wait(); R32(FLASH+0x10)=0x80;
    watchdog(); return ok && R16(address)==value;
}
static void hardware_init(void)
{
    /* Reset starts on HSI 8 MHz. Hold both heater gates and fan controls low
     * before any flash access. Actuators stay in reset state; SWD is available. */
    R32(0x40021018)|=0x18; /* GPIOB/C */
    R32(0x40010c10)=0x00c00000; /* PB6/PB7 BSRR reset */
    R32(0x40010c00)=(R32(0x40010c00)&0x00ffffff)|0x22000000;
    /* PC6/PC7: original TIM8 PWM1, active high, CCR=0 means off.
     * Floating inputs run the fans at full speed on this board. Preload low
     * before enabling GPIO outputs and retain them until the app takes over. */
    R32(0x40011010)=0x00c00000;
    R32(0x40011000)=(R32(0x40011000)&0x00ffffff)|0x22000000;
    R32(0x4002101c)|=0x80001; /* UART4 + TIM2 */
    R32(0x40011004)=(R32(0x40011004)&~0x0000ff00u)|0x00004b00;
    R32(UART+12)=0; R32(UART+8)=69; R32(UART+16)=0; R32(UART+20)=0;
    R32(UART+12)=0x200c; /* 115200 8N1, HSI / 69 = 115942 baud */
    R32(TIM2+0x28)=7999; R32(TIM2+0x2c)=65535; R32(TIM2+0x14)=1; R32(TIM2)=1;
}
static int tx_wait(uint32_t mask)
{
    uint16_t start=ticks();
    while (!(R32(UART)&mask)) { watchdog(); if ((uint16_t)(ticks()-start)>100) return 0; }
    return 1;
}
static void reply(uint8_t op,uint16_t sequence,uint8_t status,uint32_t offset,uint32_t size,uint32_t crc)
{
    uint8_t p[SH03_BOOT_REPLY_SIZE],frame[SH03_WIRE_FRAME];
    sh03_boot_reply(p,op,status,offset,size,crc);
    unsigned n=sh03_wire_encode(frame,SH03_BOOT_REPLY,sequence,p,sizeof p);
    for (unsigned i=0;i<n;++i) { if (!tx_wait(0x80)) return; R32(UART+4)=frame[i]; }
    (void)tx_wait(0x40);
}
__attribute__((noreturn,noinline)) static void jump_app(void)
{
    uint32_t sp=R32(SH03_APP_BASE),pc=R32(SH03_APP_BASE+4);
    R32(UART+12)=0; R32(TIM2)=0; R32(FLASH+0x10)=0x80;
    R32(0xe000e010)=0; R32(0xe000e018)=0;
    for (unsigned i=0;i<2;++i) { R32(0xe000e180+4*i)=0xffffffff; R32(0xe000e280+4*i)=0xffffffff; }
    R32(0xe000ed04)=(1u<<27)|(1u<<25); /* PendSV / SysTick pending clear */
    R32(0xe000ed08)=SH03_APP_BASE;
    __asm volatile("dsb\n isb\n movs r2,#0\n msr basepri,r2\n msr control,r2\n msr msp,%0\n cpsie i\n bx %1" :: "r"(sp),"r"(pc):"r2","memory");
    __builtin_unreachable();
}
void Boot_Reset(void)
{
    __asm volatile("cpsid i");
    uint32_t *source=&_data_load;
    for (uint32_t *p=&_data_start;p<&_data_end;) *p++=*source++;
    for (uint32_t *p=&_bss_start;p<&_bss_end;) *p++=0;
    hardware_init();
    /* STM32F1 ES0340: DBGMCU_IDCODE reads as zero without a debugger.
     * This loader targets the externally verified 256-KiB F1-HD board.
     * Check flash size and ARM Cortex-M3 CPUID in normal operation; reject
     * a conflicting device ID when debug identification is available.
     * CPUID mask ignores only core variant and revision. */
    uint32_t device_id=R32(0xe0042000);
    int hardware_ok=R16(0x1ffff7e0)==256 &&
        (R32(0xe000ed00)&0xff0ffff0u)==0x410fc230u &&
        (device_id==0 || (device_id&0xfff)==0x414);
    int ready=hardware_ok && sh03_install_pending();
    Sh03Update update={0}; Sh03WireParser parser={0};
    uint16_t activity=ticks(),last_rx=activity; uint8_t held=0;
    for (;;) {
        watchdog();
        uint16_t now=ticks();
        if ((uint16_t)(now-last_rx)>500) parser.used=0;
        /* Allow the co-powered ESP32 to boot and send its local recovery
         * HELLO before Wi-Fi is ready. HELLO renews the 30-second lease. */
        if (ready && (uint16_t)(now-activity)>(held?30000:10000)) jump_app();
        uint32_t sr=R32(UART);
        if (!(sr&0x2f)) continue;
        uint8_t byte=R32(UART+4),frame[SH03_WIRE_FRAME]; last_rx=now;
        if (sr&0x0f) { parser.used=0; continue; }
        if (!sh03_wire_feed(&parser,byte,frame)) continue;
        uint8_t op=frame[3],n=frame[6],status=SH03_UPDATE_RANGE;
        uint16_t sequence=sh03_get16(frame+4); const uint8_t *p=frame+7;
        if (op<SH03_BOOT_ENTER || op>SH03_BOOT_RUN) continue;
        held=1;
        if (!hardware_ok) status=SH03_UPDATE_HARDWARE;
        else if ((op==SH03_BOOT_ENTER || op==SH03_BOOT_HELLO) && !n) status=SH03_UPDATE_OK;
        else if (op==SH03_BOOT_BEGIN && n==16) status=sh03_update_begin(&update,p);
        else if (op==SH03_BOOT_DATA) status=sh03_update_data(&update,p,n);
        else if (op==SH03_BOOT_END && !n) status=sh03_update_end(&update);
        else if (op==SH03_BOOT_RUN && !n) status=sh03_active_valid()?SH03_UPDATE_OK:SH03_UPDATE_STATE;
        activity=ticks();
        reply(op,sequence,status,update.received,op==SH03_BOOT_HELLO?SH03_SLOT_SIZE:update.size,
              op==SH03_BOOT_HELLO?1:update.crc);
        if (op==SH03_BOOT_END && status==SH03_UPDATE_OK) {
            ready=sh03_install_pending();
            if (ready) jump_app();
            /* Pending stays committed. A reset retries the installation. */
        }
        if (op==SH03_BOOT_RUN && status==SH03_UPDATE_OK) jump_app();
    }
}
