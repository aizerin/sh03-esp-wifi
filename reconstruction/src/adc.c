#include "sh03_peripherals.h"

typedef Sh03AdcConfig AdcConfig;
_Static_assert(sizeof(AdcConfig)==20,"ADC config ABI");

void sh03_adc_enable(uint32_t adc,uint8_t enabled)
{
    if (enabled) SH03_U32(adc+8)|=1; else SH03_U32(adc+8)&=0xfffffffe;
}
void sh03_adc_dma(uint32_t adc,uint8_t enabled)
{
    if (enabled) SH03_U32(adc+8)|=0x100; else SH03_U32(adc+8)&=0xfffffeff;
}
int sh03_adc_calibration_pending(uint32_t adc) { return (SH03_U32(adc+8)&4)!=0; }
int sh03_adc_reset_pending(uint32_t adc) { return (SH03_U32(adc+8)&8)!=0; }
void sh03_adc_reset_calibration(uint32_t adc) { SH03_U32(adc+8)|=8; }
void sh03_adc_start_calibration(uint32_t adc) { SH03_U32(adc+8)|=4; }
void sh03_adc_software_start(uint32_t adc,uint8_t enabled)
{
    if (enabled) SH03_U32(adc+8)|=0x500000; else SH03_U32(adc+8)&=0xffafffff;
}
void sh03_adc_init(uint32_t adc,const AdcConfig *config)
{
    SH03_U32(adc+4)=(SH03_U32(adc+4)&0xfff0feff)|config->mode|((uint32_t)config->scan<<8);
    SH03_U32(adc+8)=(SH03_U32(adc+8)&0xfff1f7fd)|config->alignment|config->external_trigger|((uint32_t)config->continuous<<1);
    SH03_U32(adc+44)=(SH03_U32(adc+44)&0xff0fffff)|((uint32_t)(uint8_t)(config->channel_count-1)<<20);
}
static uint32_t lsl(uint32_t value,uint8_t shift) { return shift<32?value<<shift:0; }
void sh03_adc_channel(uint32_t adc,uint8_t channel,uint8_t rank,uint8_t sample_time)
{
    uint32_t reg=adc+(channel<10?16:12);
    uint8_t shift=channel<10?channel*3:channel*3-30;
    SH03_U32(reg)=(SH03_U32(reg)&~lsl(7,shift))|lsl(sample_time,shift);
    reg=adc+(rank<7?52:rank<13?48:44);
    shift=rank*5-(rank<7?5:rank<13?35:65);
    SH03_U32(reg)=(SH03_U32(reg)&~lsl(31,shift))|lsl(channel,shift);
}
