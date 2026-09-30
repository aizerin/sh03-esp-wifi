/* Board wiring comes from the recovered startup descriptors, not a guessed
 * schematic. See docs/hardware.md for pins and the original RAM addresses.
 * Unassigned fields in vendor stack-local configs are explicitly zero here.
 */
#include "sh03_peripherals.h"

typedef struct {
    uint32_t pwm_clock,capture_clock,reserved0,pwm_port,pwm_pins,reserved1,capture_port,capture_pins;
    uint32_t pwm_timer;
    uint16_t pwm_mode; uint8_t pwm_channel,reserved2;
    uint32_t compare_function,capture_timer;
    uint16_t capture_mode; uint8_t capture_channel,reserved3;
    uint32_t dma,dma_memory,dma_peripheral,dma_count,capture_state,buffer,enabled;
} FanHardware;
typedef struct {
    uint32_t gpio_clock,port,pins,adc_clock,adc;
    uint8_t channel,reserved[3];
    uint32_t dma_controller,dma,reserved_tail;
} AdcHardware;
typedef struct { Sh03Gpio gpio; uint32_t timer; uint8_t channel,reserved[3]; uint32_t compare_function; } HeaterHardware;
_Static_assert(sizeof(FanHardware)==80,"fan descriptor ABI");
_Static_assert(sizeof(AdcHardware)==36,"ADC descriptor ABI");
_Static_assert(sizeof(HeaterHardware)==24,"heater descriptor ABI");
#define fan_hardware ((const FanHardware *)0x20000034)
#define hall_hardware ((const AdcHardware *)0x20000150)
#define ntc_hardware ((const AdcHardware *)0x20000210)
#define heater_hardware ((const HeaterHardware *)0x200001e0)

static void output_channel(uint32_t timer,uint8_t channel,const Sh03TimerOutput *c)
{
    static const uint32_t initializers[]={0x08007120,0x080072c0,0x08007440,0x080075bc};
    static const uint32_t preloads[]={0x08007284,0x08007400,0x08007580,0x080076ac};
    if (channel==0 || channel==4 || channel==8 || channel==12) {
        SH03_FN(initializers[channel/4],void,uint32_t,const Sh03TimerOutput *)(timer,c);
        SH03_FN(preloads[channel/4],void,uint32_t,uint16_t)(timer,8);
    }
}
void sh03_fan_hardware_init(void)
{
    fw_apb2_clock(fan_hardware[0].pwm_clock,1);
    fw_apb2_clock(fan_hardware[0].capture_clock,1);
    fw_apb2_clock(0x2000,1);fw_apb2_clock(0x800,1);fw_ahb_clock(1,1);
    Sh03TimerBase base={71,0,999,0,0,0};
    fw_timer_base(fan_hardware[0].pwm_timer,&base);
    base.period=65535;fw_timer_base(fan_hardware[0].capture_timer,&base);
    for (unsigned ch=0;ch<2;++ch) {
        const FanHardware *h=&fan_hardware[ch];
        fw_gpio_configure(h->pwm_port,h->pwm_pins,0x18,1);
        const Sh03TimerOutput output={.mode=0x60,.output_enabled=1};
        output_channel(h->pwm_timer,h->pwm_channel,&output);
        fw_gpio_configure(h->capture_port,h->capture_pins,4,0);
        const Sh03TimerInput input={h->capture_channel,0,1,0,8};
        SH03_FN(0x0800702c,void,uint32_t,const Sh03TimerInput *)(h->capture_timer,&input);
        const Sh03DmaConfig dma={h->dma_peripheral,h->dma_memory,0,h->dma_count,0,0x80,0x100,0x400,0,0x2000,0};
        fw_dma_init(h->dma,&dma);fw_dma_enable(h->dma,0);
        fw_timer_dma(h->capture_timer,ch==0?0x200:0x400,0);
        fw_timer_preload(h->capture_timer,1);
    }
    fw_timer_enable(fan_hardware[0].capture_timer,1);
    fw_timer_enable(fan_hardware[0].pwm_timer,1);
    SH03_FN(0x08006fc4,void,uint32_t,uint8_t)(fan_hardware[0].pwm_timer,1);
}
void sh03_heater_timer_configure(uint32_t timer,uint16_t mode,uint8_t channel)
{
    const Sh03TimerBase base={719,0,3999,0,0,0};
    const Sh03TimerOutput output={.mode=mode,.output_enabled=1};
    fw_timer_base(timer,&base);output_channel(timer,channel,&output);
    fw_timer_preload(timer,1);fw_timer_enable(timer,1);
}
void sh03_heater_hardware_init(void)
{
    fw_apb2_clock(heater_hardware[0].gpio.clock_mask,1);fw_apb1_clock(4,1);
    for (unsigned ch=0;ch<2;++ch) {
        const HeaterHardware *h=&heater_hardware[ch];
        fw_gpio_configure(h->gpio.port,h->gpio.pin_mask,0x18,1);
        SH03_FN(0x080040c4,void,uint32_t,uint16_t,uint8_t)(h->timer,0x60,h->channel);
    }
}
void sh03_actuator_hardware_init(void)
{
    const Sh03Gpio *gpio=(const Sh03Gpio *)0x200000f0;
    for (unsigned i=0;i<8;++i) fw_gpio_configure(gpio[i].port,gpio[i].pin_mask,0x10,1);
}
static void adc_calibrate(uint32_t adc)
{
    SH03_FN(0x080018a8,void,uint32_t)(adc);
    while (SH03_FN(0x08001660,int,uint32_t)(adc)) { }
    SH03_FN(0x080018e8,void,uint32_t)(adc);
    while (SH03_FN(0x08001630,int,uint32_t)(adc)) { }
}
static void adc_hardware_init(const AdcHardware *h,uint8_t ntc)
{
    unsigned count=ntc?2:4;
    fw_apb2_clock(h[0].gpio_clock,1);
    for (unsigned i=0;i<count;++i) fw_gpio_configure(h[i].port,h[i].pins,0,1);
    fw_ahb_clock(ntc?1:2,1);
    const Sh03DmaConfig dma={h[0].adc+0x4c,ntc?0x200006d4:0x200006ca,0,count,0,0x80,0x100,0x400,ntc?0x20:0,0x2000,0};
    fw_dma_init(h[0].dma,&dma);
    if (ntc) SH03_FN(0x08005a44,void,uint32_t)(0x8000);
    fw_apb2_clock(h[0].adc_clock,1);
    const Sh03AdcConfig config={.scan=1,.continuous=ntc,.external_trigger=0xe0000,.channel_count=count};
    fw_adc_init(h[0].adc,&config);
    for (unsigned i=0;i<count;++i) fw_adc_channel(h[i].adc,h[i].channel,i+1,ntc?7:5);
    fw_adc_dma(h[0].adc,1);fw_dma_enable(h[0].dma,ntc);fw_adc_enable(h[0].adc,1);
    adc_calibrate(h[0].adc);
    if (ntc) fw_adc_start(h[0].adc,1);
}
void sh03_hall_adc_hardware_init(void) { adc_hardware_init(hall_hardware,0); }
void sh03_ntc_adc_hardware_init(void) { adc_hardware_init(ntc_hardware,1); }

void sh03_touch_irq_init(void)
{
    fw_apb2_clock(SH03_U32(0x2000025c),1);fw_apb2_clock(1,1);
    fw_gpio_configure(SH03_U32(0x20000260),SH03_U16(0x20000264),4,0);
    SH03_FN(0x080038a8,void,uint8_t,uint8_t)(SH03_U8(0x20000268),SH03_U8(0x20000269));
    const Sh03ExtiConfig exti={8,0,0x10,1,0};
    SH03_FN(0x080033b8,void,const Sh03ExtiConfig *)(&exti);
    const Sh03NvicConfig irq={9,6,0,1};fw_nvic_init(&irq);
}
void sh03_alarm_hardware_init(void)
{
    const Sh03Gpio *gpio=(const Sh03Gpio *)0x20000284;
    fw_apb2_clock(gpio->clock_mask,1);fw_gpio_configure(gpio->port,gpio->pin_mask,0x10,1);
    SH03_FN(0x0800ad1c,void,void)();
}
void sh03_aux_output_init(void)
{
    const Sh03Gpio *gpio=(const Sh03Gpio *)0x20000290;
    fw_apb2_clock(8,1);fw_gpio_configure(gpio->port,gpio->pin_mask,0x10,1);
    SH03_U32(sh03_gpio_alias(gpio,12))=0;
}
void sh03_aux_output(uint8_t enabled)
{
    SH03_U32(sh03_gpio_alias((const Sh03Gpio *)0x20000290,12))=enabled?0:1;
}
void sh03_delay_timer_init(void)
{
    fw_apb1_clock(0x10,1);
    const Sh03TimerBase config={71,0,65535,0,0,0};fw_timer_base(0x40001000,&config);
}
void sh03_alarm_timer_init(void)
{
    fw_apb1_clock(0x20,1);
    const Sh03TimerBase config={7199,0,25,0,0,0};fw_timer_base(0x40001400,&config);
    fw_timer_preload(0x40001400,1);fw_ahb_clock(2,1);
    const Sh03DmaConfig dma={0x40010c10,0x20000030,0x10,1,0,0,0x200,0x800,0,0x2000,0};
    fw_dma_init(0x40020444,&dma);
    const Sh03NvicConfig irq={59,5,0,1};fw_nvic_init(&irq);
    fw_dma_irq_config(0x40020444,2,1);fw_dma_enable(0x40020444,0);
    fw_timer_dma(0x40001400,0x100,1);fw_timer_enable(0x40001400,0);
}
void sh03_debug_uart_init(void)
{
    fw_apb2_clock(0x10,1);fw_apb1_clock(0x80000,1);
    Sh03GpioConfig gpio={0x400,3,0x18};
    SH03_FN(0x080038fc,void,uint32_t,const Sh03GpioConfig *)(0x40011000,&gpio);
    gpio.pins=0x800;gpio.mode=4;
    SH03_FN(0x080038fc,void,uint32_t,const Sh03GpioConfig *)(0x40011000,&gpio);
    SH03_FN(0x0800aef4,void,uint32_t)(0x40004c00);
    const Sh03UartConfig uart={.baud=115200,.mode=12};
    SH03_FN(0x0800b008,void,uint32_t,const Sh03UartConfig *)(0x40004c00,&uart);
    SH03_FN(0x0800aec8,void,uint32_t,uint8_t)(0x40004c00,1);
}
