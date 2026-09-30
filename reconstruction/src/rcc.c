#include "sh03_peripherals.h"

static void update_bits(uint32_t reg,uint32_t mask,uint8_t enabled)
{
    if (enabled) SH03_U32(reg)|=mask; else SH03_U32(reg)&=~mask;
}
void sh03_rcc_adc_clock(uint32_t divider) { SH03_U32(0x40021004)=(SH03_U32(0x40021004)&0xffff3fff)|divider; }
void sh03_rcc_ahb_clock(uint32_t mask,uint8_t enabled) { update_bits(0x40021014,mask,enabled); }
void sh03_rcc_apb1_clock(uint32_t mask,uint8_t enabled) { update_bits(0x4002101c,mask,enabled); }
void sh03_rcc_apb1_reset(uint32_t mask,uint8_t enabled) { update_bits(0x40021010,mask,enabled); }
void sh03_rcc_apb2_clock(uint32_t mask,uint8_t enabled) { update_bits(0x40021018,mask,enabled); }
void sh03_rcc_apb2_reset(uint32_t mask,uint8_t enabled) { update_bits(0x4002100c,mask,enabled); }

void sh03_rcc_frequencies(Sh03ClockFrequencies *out)
{
    static const uint8_t shifts[16]={0,0,0,0,1,2,3,4,1,2,3,4,6,7,8,9};
    uint32_t source=SH03_U32(0x40021004)&12;
    if (source==8) {
        uint32_t multiplier=((SH03_U32(0x40021004)&0x3c0000)>>18)+2;
        if (!(SH03_U32(0x40021004)&0x10000)) out->sysclk=multiplier*4000000;
        else out->sysclk=multiplier*((SH03_U32(0x40021004)&0x20000)?4000000:8000000);
    } else out->sysclk=8000000;
    out->hclk=out->sysclk>>shifts[(SH03_U32(0x40021004)&0xf0)>>4];
    out->pclk1=out->hclk>>shifts[(SH03_U32(0x40021004)&0x700)>>8];
    out->pclk2=out->hclk>>shifts[(SH03_U32(0x40021004)&0x3800)>>11];
    out->adcclk=out->pclk2/(2+2*((SH03_U32(0x40021004)&0xc000)>>14));
}

void sh03_clock_configure_72mhz(void)
{
    uint32_t attempts=0,status;
    SH03_U32(0x40021000)|=0x10000;
    do {
        status=SH03_U32(0x40021000);++attempts;
    } while (!(status&0x20000) && attempts<0x500);
    if (SH03_U32(0x40021000)&0x20000) {
        SH03_U32(0x40022000)|=0x10;
        SH03_U32(0x40022000)&=0xfffffff8;
        SH03_U32(0x40022000)|=2;
        SH03_U32(0x40021004)=SH03_U32(0x40021004);
        SH03_U32(0x40021004)=SH03_U32(0x40021004);
        SH03_U32(0x40021004)|=0x400;
        SH03_U32(0x40021004)&=0xffc0ffff;
        SH03_U32(0x40021004)|=0x1d0000;
        SH03_U32(0x40021000)|=0x1000000;
        while (!(SH03_U32(0x40021000)&0x2000000)) { }
        SH03_U32(0x40021004)&=0xfffffffc;
        SH03_U32(0x40021004)|=2;
        while ((SH03_U32(0x40021004)&12)!=8) { }
    }
}
void sh03_clock_configure(void) { SH03_FN(0x080067d0,void,void)(); }
void sh03_system_init(void)
{
    SH03_U32(0x40021000)|=1;
    SH03_U32(0x40021004)&=0xf8ff0000;
    SH03_U32(0x40021000)&=0xfef6ffff;
    SH03_U32(0x40021000)&=0xfffbffff;
    SH03_U32(0x40021004)&=0xff80ffff;
    SH03_U32(0x40021008)=0x9f0000;
    SH03_FN(0x080067c8,void,void)();
    SH03_U32(0xe000ed08)=0x08000000;
}
