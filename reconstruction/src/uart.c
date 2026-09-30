#include "sh03_peripherals.h"

void sh03_uart_enable(uint32_t uart,uint8_t enabled)
{
    if (enabled) SH03_U16(uart+12)|=0x2000; else SH03_U16(uart+12)&=0xdfff;
}
void sh03_uart_deinit(uint32_t uart)
{
    if (uart==0x40013800) {
        SH03_FN(0x08005b50,void,uint32_t,uint8_t)(0x4000,1);
        SH03_FN(0x08005b50,void,uint32_t,uint8_t)(0x4000,0);
    } else {
        uint32_t mask=uart==0x40004400?0x20000:uart==0x40004800?0x40000:
                      uart==0x40004c00?0x80000:uart==0x40005000?0x100000:0;
        if (mask) { fw_apb1_reset(mask,1); fw_apb1_reset(mask,0); }
    }
}
int sh03_uart_flag(uint32_t uart,uint16_t mask) { return (SH03_U16(uart)&mask)!=0; }
void sh03_uart_send(uint32_t uart,uint16_t value) { SH03_U16(uart+4)=value&0x1ff; }
void sh03_debug_putc(uint8_t value)
{
    for (uint32_t attempts=0;attempts<1000001;++attempts) {
        if (SH03_FN(0x0800afc4,int,uint32_t,uint16_t)(0x40004c00,0x80)) {
            SH03_FN(0x0800b164,void,uint32_t,uint16_t)(0x40004c00,value);return;
        }
    }
}
void sh03_debug_puts(const char *text)
{
    /* Original narrows strlen to uint8_t before transmitting. */
    uint32_t length=0;while (text[length]) ++length;
    for (unsigned i=0;i<(uint8_t)length;++i) SH03_FN(0x0800adf8,void,uint8_t)(text[i]);
}
void sh03_uart_init(uint32_t uart,const Sh03UartConfig *c)
{
    SH03_U16(uart+16)=(SH03_U16(uart+16)&0xcfff)|c->stop_bits;
    SH03_U16(uart+12)=(SH03_U16(uart+12)&0xe9f3)|c->mode|c->word_length|c->parity;
    SH03_U16(uart+20)=(SH03_U16(uart+20)&0xfcff)|c->flow_control;
    Sh03ClockFrequencies clocks;
    fw_clock_frequencies(&clocks);
    uint32_t clock=uart==0x40013800?clocks.pclk2:clocks.pclk1;
    uint32_t divisor=c->baud*((SH03_U16(uart+12)&0x8000)?2u:4u);
    /* Cortex-M3 UDIV returns zero for a zero divisor with DIV_0_TRP clear. */
    uint32_t scaled=divisor?(clock*25u)/divisor:0;
    int oversampling8=(SH03_U16(uart+12)&0x8000)!=0;
    uint32_t fraction=(scaled%100)*(oversampling8?8u:16u)+50;
    uint32_t mask=oversampling8?7u:15u;
    SH03_U16(uart+8)=((scaled/100)<<4)|((fraction/100)&mask);
}
