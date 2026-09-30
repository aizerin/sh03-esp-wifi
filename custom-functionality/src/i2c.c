#include "sh03_peripherals.h"

void sh03_i2c_dma_configure(uint32_t clock,uint32_t channel,uint32_t peripheral,uint32_t direction,uint8_t irq_number)
{
    fw_ahb_clock(clock,1);fw_dma_deinit(channel);
    /* Memory/count were uninitialized in the original; both are set by each
     * transfer before enabling DMA. Define the disabled-channel state as zero. */
    const Sh03DmaConfig dma={peripheral,0,direction,0,0,0x80,0,0,0,0x2000,0};
    fw_dma_init(channel,&dma);
    const Sh03NvicConfig irq={irq_number,6,0,1};fw_nvic_init(&irq);
    fw_dma_irq_config(channel,2,1);fw_dma_enable(channel,0);
}
void sh03_i2c_bus_initialize(Sh03I2cBus *bus)
{
    if (!bus) return;
    const uint32_t *hardware=(const uint32_t *)(0x08011038+bus->index*32u);
    const uint32_t *dma=(const uint32_t *)(0x08011014+bus->index*20u);
    uint32_t clock=hardware[1],scl_port=hardware[2],scl_pin=hardware[3],sda_port=hardware[5],sda_pin=hardware[6];
    bus->peripheral=hardware[0];bus->tx_dma=dma[0];bus->rx_dma=dma[1];
    SH03_HANDLE(0x20000338)=fw_queue_create(1,0,3);bus->completion=SH03_HANDLE(0x20000338);
    fw_apb2_clock(clock,1);
    fw_gpio_configure(scl_port,scl_pin,0x1c,1);fw_gpio_configure(sda_port,sda_pin,0x1c,1);
    SH03_FN(0x08003b80,void,uint32_t,uint16_t)(scl_port,scl_pin);
    SH03_FN(0x08003b80,void,uint32_t,uint16_t)(sda_port,sda_pin);
    SH03_FN(0x0800b8ac,void,uint32_t,uint32_t,uint32_t,uint32_t,uint8_t)(1,bus->tx_dma,bus->peripheral+16,0x10,14);
    SH03_FN(0x0800b8ac,void,uint32_t,uint32_t,uint32_t,uint32_t,uint8_t)(1,bus->rx_dma,bus->peripheral+16,0,15);
    SH03_FN(0x0800bbb8,void,const Sh03I2cBus *)(bus);
}

int sh03_i2c_event(uint32_t peripheral,uint32_t event)
{
    uint32_t status=SH03_U16(peripheral+20);
    status|=(SH03_U16(peripheral+24)&255u)<<16;
    return (status&event)==event;
}
void sh03_i2c_enable(uint32_t peripheral,uint8_t enabled)
{
    if (enabled) SH03_U16(peripheral)|=1; else SH03_U16(peripheral)&=0xfffe;
}
void sh03_i2c_dma(uint32_t peripheral,uint8_t enabled)
{
    if (enabled) SH03_U16(peripheral+4)|=0x800; else SH03_U16(peripheral+4)&=0xf7ff;
}
void sh03_i2c_last_transfer(uint32_t peripheral,uint8_t enabled)
{
    if (enabled) SH03_U16(peripheral+4)|=0x1000; else SH03_U16(peripheral+4)&=0xefff;
}
void sh03_i2c_start(uint32_t peripheral,uint8_t enabled)
{
    if (enabled) SH03_U16(peripheral)|=0x100; else SH03_U16(peripheral)&=0xfeff;
}
void sh03_i2c_stop(uint32_t peripheral,uint8_t enabled)
{
    if (enabled) SH03_U16(peripheral)|=0x200; else SH03_U16(peripheral)&=0xfdff;
}
void sh03_i2c_address(uint32_t peripheral,uint8_t address,uint8_t receive)
{
    SH03_U16(peripheral+16)=receive?(address|1):(address&0xfe);
}
void sh03_i2c_deinit(uint32_t peripheral)
{
    uint32_t mask=peripheral==0x40005400?0x200000:0x400000;
    fw_apb1_reset(mask,1);fw_apb1_reset(mask,0);
}
void sh03_i2c_init(uint32_t peripheral,const Sh03I2cConfig *config)
{
    uint16_t cr2=SH03_U16(peripheral+4)&0xffc0;
    Sh03ClockFrequencies clocks;
    fw_clock_frequencies(&clocks);
    uint16_t mhz=(uint16_t)(clocks.pclk1/1000000);
    SH03_U16(peripheral+4)=cr2|mhz;
    SH03_U16(peripheral)&=0xfffe;
    uint16_t divider;
    if (config->clock_hz<=100000) {
        /* Cortex-M3 UDIV returns zero for a zero divisor in the original. */
        uint32_t denominator=config->clock_hz*2;
        divider=denominator?(uint16_t)(clocks.pclk1/denominator):0;
        if (divider<4) divider=4;
        SH03_U16(peripheral+32)=mhz+1;
    } else {
        if (config->duty==0xbfff) divider=(uint16_t)(clocks.pclk1/(config->clock_hz*3));
        else divider=(uint16_t)(clocks.pclk1/(config->clock_hz*25))|0x4000;
        if ((divider&0xfff)==0) divider|=1;
        divider|=0x8000;
        SH03_U16(peripheral+32)=(uint16_t)((clocks.pclk1/1000000)*300/1000)+1;
    }
    SH03_U16(peripheral+28)=divider;
    SH03_U16(peripheral)|=1;
    SH03_U16(peripheral)=(SH03_U16(peripheral)&0xfbf5)|config->ack|config->mode;
    SH03_U16(peripheral+8)=config->address_mode|config->own_address;
}

static uint8_t i2c_transfer(const Sh03I2cBus *bus,uint8_t address,void *data,uint32_t length,uint32_t timeout,uint8_t receive)
{
    uint32_t started=fw_ticks();
    fw_i2c_dma(bus->peripheral,0);
    uint32_t dma=receive?bus->rx_dma:bus->tx_dma;
    fw_dma_enable(dma,0);
    SH03_U32(dma+12)=(uintptr_t)data;
    SH03_U32(dma+4)=length+(receive?0:1);
    fw_i2c_start(bus->peripheral,1);
    while (!fw_i2c_event(bus->peripheral,0x30001)) {
        if (fw_ticks()-started>timeout) { fw_i2c_stop(bus->peripheral,1);return 3; }
    }
    fw_i2c_address(bus->peripheral,address,receive);
    while (!fw_i2c_event(bus->peripheral,receive?0x30002:0x70082)) {
        if (fw_ticks()-started>timeout) { fw_i2c_stop(bus->peripheral,1);return 2; }
    }
    if (receive) fw_i2c_last_transfer(bus->peripheral,1);
    fw_i2c_dma(bus->peripheral,1);
    fw_dma_enable(dma,1);
    uint32_t elapsed=fw_ticks()-started;
    if (fw_semaphore_take(bus->completion,elapsed<timeout?timeout-elapsed:0)!=1) {
        fw_i2c_stop(bus->peripheral,1);return 4;
    }
    return 0;
}
uint8_t sh03_i2c_read(const Sh03I2cBus *bus,uint8_t address,void *data,uint32_t length,uint32_t timeout) { return i2c_transfer(bus,address,data,length,timeout,1); }
uint8_t sh03_i2c_write(const Sh03I2cBus *bus,uint8_t address,const void *data,uint32_t length,uint32_t timeout) { return i2c_transfer(bus,address,(void *)data,length,timeout,0); }

void sh03_i2c_bus_configure(const Sh03I2cBus *bus)
{
    fw_apb1_clock(((const uint32_t *)0x08011034)[bus->index*8u],1);
    fw_i2c_deinit(bus->peripheral);
    const Sh03I2cConfig config={100000,0,0xbfff,0,0x400,0x4000,0};
    fw_i2c_init(bus->peripheral,&config);
    fw_i2c_enable(bus->peripheral,1);
}
