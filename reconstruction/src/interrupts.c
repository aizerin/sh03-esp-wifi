#include "sh03_peripherals.h"

void sh03_nvic_priority_group(uint32_t group)
{
    SH03_U32(0xe000ed0c)=(SH03_U32(0xe000ed0c)&0xf8ff)|((group&7)<<8)|0x05fa0000;
}
void sh03_nvic_init(const Sh03NvicConfig *c)
{
    if (c->enabled) {
        uint32_t group=((~SH03_U32(0xe000ed0c))&0x7ff)>>8;
        uint8_t shift=4-group;
        uint32_t preemption=shift<32?(uint32_t)c->preemption_priority<<shift:0;
        SH03_U8(0xe000e400+c->irq)=(preemption|((15u>>group)&c->subpriority))<<4;
        SH03_U32(0xe000e100+4u*(c->irq/32))=1u<<(c->irq%32);
    } else SH03_U32(0xe000e180+4u*(c->irq/32))=1u<<(c->irq%32);
}
