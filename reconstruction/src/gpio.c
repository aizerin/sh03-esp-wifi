#include "sh03_peripherals.h"

typedef struct { uint16_t pins; uint8_t speed,mode; } GpioConfig;
typedef struct { uint32_t lines; uint8_t mode,trigger,enabled,reserved; } ExtiConfig;

void sh03_gpio_init(uint32_t port,const GpioConfig *config)
{
    uint32_t mode=config->mode&15;
    if (config->mode&0x10) mode|=config->speed;
    for (unsigned half=0;half<2;++half) {
        if ((half==0 && (config->pins&255)==0) || (half==1 && config->pins<256)) continue;
        uint32_t value=SH03_U32(port+half*4);
        for (unsigned i=0;i<8;++i) {
            uint32_t pin=1u<<(i+half*8);
            if ((config->pins&pin)==pin) {
                value=(value&~(15u<<(i*4)))|(mode<<(i*4));
                if (config->mode==0x28) SH03_U32(port+20)=pin;
                else if (config->mode==0x48) SH03_U32(port+16)=pin;
            }
        }
        SH03_U32(port+half*4)=value;
    }
}

void sh03_gpio_configure(uint32_t port,uint16_t pins,uint8_t mode,uint8_t speed)
{
    /* The original wrapper leaves speed uninitialized for speed==0. Actual
     * callers use zero only for input modes, which ignore this byte. */
    const GpioConfig config={pins,speed,mode};
    SH03_FN(0x080038fc,void,uint32_t,const GpioConfig *)(port,&config);
}

void sh03_gpio_exti_source(uint8_t port,uint8_t pin)
{
    uint32_t reg=0x40010008+(pin&0xfcu),shift=(pin&3u)*4;
    SH03_U32(reg)&=~(15u<<shift);
    SH03_U32(reg)|=(uint32_t)port<<shift;
}

void sh03_gpio_remap(uint32_t remap,uint8_t enabled)
{
    uint32_t reg=(remap&0x80000000)?0x4001001c:0x40010004;
    uint32_t value=SH03_U32(reg);
    unsigned shift=(remap>>17)&0xf0;
    uint32_t bits=shift<32?(remap&0xffff)<<shift:0;
    if ((remap&0x300000)==0x300000) {
        value&=0xf0ffffff;
        SH03_U32(0x40010004)&=0xf0ffffff;
    } else {
        uint32_t mask=(remap&0x100000)?3u<<((remap&0xfffff)>>16):bits;
        value=(value&~mask)|0x0f000000;
    }
    if (enabled) value|=bits;
    SH03_U32(reg)=value;
}
void sh03_gpio_set_bits(uint32_t port,uint16_t pins) { SH03_U32(port+16)=pins; }
void sh03_exti_clear(uint32_t lines) { SH03_U32(0x40010414)=lines; }
uint8_t sh03_exti_pending(uint32_t lines)
{
    uint32_t enabled=SH03_U32(0x40010400),pending=SH03_U32(0x40010414);
    return (pending&lines)!=0 && (enabled&lines)!=0;
}
void sh03_exti_init(const ExtiConfig *config)
{
    uint32_t mask=0x40010400+config->mode;
    if (!config->enabled) SH03_U32(mask)&=~config->lines;
    else {
        SH03_U32(0x40010400)&=~config->lines;
        SH03_U32(0x40010404)&=~config->lines;
        SH03_U32(mask)|=config->lines;
        SH03_U32(0x40010408)&=~config->lines;
        SH03_U32(0x4001040c)&=~config->lines;
        if (config->trigger==0x10) {
            SH03_U32(0x40010408)|=config->lines;
            SH03_U32(0x4001040c)|=config->lines;
        } else SH03_U32(0x40010400+config->trigger)|=config->lines;
    }
}

void sh03_boot_beep(void)
{
    SH03_U32(0x40020448)=1;
    SH03_U16(0x40001424)=0;
    uint32_t odr=SH03_U32(0x20000288)+12;
    SH03_U32((odr&0xf0000000u)+((odr&0xfffffu)<<5)+SH03_U8(0x2000028e)*4u+0x02000000)=1;
    SH03_U16(0x40001400)|=1;
    SH03_U32(0x40020444)|=1;
}

void sh03_touch_irq(void)
{
    if (SH03_FN(0x0800336c,uint8_t,uint32_t)(9)) {
        SH03_FN(0x08003358,void,uint32_t)(9);
        uint32_t previous;
        __asm volatile("mrs %0, basepri":"=r"(previous));
        uint32_t mask=0x50;
        __asm volatile("msr basepri, %0\n isb\n dsb"::"r"(mask):"memory");
        if (SH03_U8(0x200006a4)==0) {
            if (SH03_U8(0x20000259)==0) {
                SH03_U8(0x20000259)=1;
                fw_boot_beep();
                SH03_U8(0x200006a4)=1;
            } else if (SH03_U8(0x20000259)==1) {
                SH03_U8(0x20000259)=0;
                SH03_U8(0x200006a4)=1;
            } else if (SH03_U8(0x20000259)==3) {
                SH03_U8(0x20000259)=0;
                SH03_U8(0x200006a4)=0;
            }
        }
        __asm volatile("msr basepri, %0"::"r"(previous):"memory");
    }
}
