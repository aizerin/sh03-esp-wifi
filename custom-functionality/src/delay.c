#include "sh03_peripherals.h"

void sh03_busy_delay_ms(uint16_t milliseconds)
{
    SH03_U16(0x40001024)=0;SH03_U16(0x40001028)=7199;SH03_U16(0x40001014)=1;
    SH03_U16(0x40001000)|=1;
    while (SH03_U16(0x40001024)<milliseconds*10u) { }
    SH03_U16(0x40001000)&=0xfffe;
}
void sh03_busy_delay_us(uint16_t microseconds)
{
    SH03_U16(0x40001024)=0;SH03_U16(0x40001028)=71;SH03_U16(0x40001014)=1;
    SH03_U16(0x40001000)|=1;
    while (SH03_U16(0x40001024)<microseconds) { }
    SH03_U16(0x40001000)&=0xfffe;
}
