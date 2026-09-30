#include "sh03_peripherals.h"

static uint32_t dma_controller(uint32_t flags) { return flags&0x10000000u?0x40020400u:0x40020000u; }
void sh03_dma_clear_flag(uint32_t flags) { SH03_U32(dma_controller(flags)+4)=flags; }
void sh03_dma_clear_irq(uint32_t flags) { SH03_U32(dma_controller(flags)+4)=flags; }
void sh03_dma_enable(uint32_t channel,uint8_t enabled)
{
    if (enabled) SH03_U32(channel)|=1;
    else SH03_U32(channel)&=0xfffe;
}
uint16_t sh03_dma_count(uint32_t channel) { return (uint16_t)SH03_U32(channel+4); }
void sh03_dma_set_count(uint32_t channel,uint16_t count) { SH03_U32(channel+4)=count; }
int sh03_dma_flag(uint32_t flags) { return (SH03_U32(dma_controller(flags))&flags)!=0; }
int sh03_dma_irq(uint32_t flags) { return (SH03_U32(dma_controller(flags))&flags)!=0; }
void sh03_dma_irq_config(uint32_t channel,uint32_t flags,uint8_t enabled)
{
    if (enabled) SH03_U32(channel)|=flags;
    else SH03_U32(channel)&=~flags;
}
void sh03_dma_init(uint32_t channel,const Sh03DmaConfig *config)
{
    SH03_U32(channel)=(SH03_U32(channel)&0xffff800f)|config->memory_to_memory|config->direction|config->mode|
        config->peripheral_increment|config->memory_increment|config->peripheral_width|config->memory_width|config->priority;
    SH03_U32(channel+4)=config->count;
    SH03_U32(channel+8)=config->peripheral;
    SH03_U32(channel+12)=config->memory;
}
void sh03_dma_deinit(uint32_t channel)
{
    SH03_U32(channel)&=0xfffe;
    SH03_U32(channel)=0;
    SH03_U32(channel+4)=0;
    SH03_U32(channel+8)=0;
    SH03_U32(channel+12)=0;
    for (unsigned controller=0;controller<2;++controller) {
        uint32_t base=0x40020000+controller*0x400;
        unsigned channels=controller==0?7:5;
        for (unsigned i=0;i<channels;++i) if (channel==base+8+i*20) { SH03_U32(base+4)|=15u<<(i*4);return; }
    }
}

static void i2c_dma_irq(uint32_t flag)
{
    if (fw_dma_irq(flag)!=0) {
        fw_dma_clear_irq(flag);
        fw_i2c_stop(0x40005800,1);
        int32_t woken=0;
        if (SH03_HANDLE(0x20000338)!=0) fw_semaphore_give_isr(SH03_HANDLE(0x20000338),&woken);
        if (woken) {
            SH03_U32(0xe000ed04)=0x10000000;
            __asm volatile("dsb\n isb":::"memory");
        }
    }
}
void sh03_dma1_channel4_irq(void) { i2c_dma_irq(0x2000); }
void sh03_dma1_channel5_irq(void) { i2c_dma_irq(0x20000); }
void sh03_dma2_channel4_5_irq(void)
{
    if (fw_dma_irq(0x10002000)!=0) {
        fw_dma_clear_irq(0x10002000);
        SH03_U16(0x40001400)&=0xfffe;
        SH03_U32(0x40020444)&=0xfffffffe;
    }
}
