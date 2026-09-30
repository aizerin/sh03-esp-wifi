/* STM32-style timer driver reconstructed from 0x08006b14..0x080078e7.
 * Register access width and sequence follow the dump, including two-stage
 * input prescaler writes. These functions configure both heater PWM and fan
 * capture; the board-specific parameters live in board.c.
 */
#include "sh03_peripherals.h"

static int full_timer(uint32_t timer)
{
    return timer==0x40012c00 || timer==0x40013400 || timer==0x40000000 ||
           timer==0x40000400 || timer==0x40000800 || timer==0x40000c00;
}
static int advanced_timer(uint32_t timer) { return timer==0x40012c00 || timer==0x40013400; }
static int repetition_timer(uint32_t timer)
{
    return advanced_timer(timer) || timer==0x40014000 || timer==0x40014400 || timer==0x40014800;
}
static void input_config(uint32_t timer,unsigned ch,uint16_t polarity,uint16_t selection,uint16_t filter)
{
    unsigned shift=ch*4;
    SH03_U16(timer+0x20)&=~(1u<<shift);
    uint32_t reg=timer+0x18+(ch/2)*4;
    uint16_t ccmr=SH03_U16(reg);
    uint16_t ccer=SH03_U16(timer+0x20);
    if (full_timer(timer)) ccer=(ccer&~(2u<<shift))|((uint32_t)polarity<<shift);
    else {
        /* The last channel's original mask is 0x7dff, not 0x5fff. */
        uint16_t mask=ch==3?0x7dff:(uint16_t)~(10u<<shift);
        ccer=(ccer&mask)|polarity;
    }
    unsigned field_shift=(ch%2)*8;
    SH03_U16(reg)=(ccmr&~(0xf3u<<field_shift))|
                  ((uint32_t)selection<<field_shift)|((uint32_t)filter<<(field_shift+4));
    SH03_U16(timer+0x20)=ccer|(1u<<shift);
}
void sh03_timer_input1(uint32_t t,uint16_t p,uint16_t s,uint16_t f) { input_config(t,0,p,s,f); }
void sh03_timer_input2(uint32_t t,uint16_t p,uint16_t s,uint16_t f) { input_config(t,1,p,s,f); }
void sh03_timer_input3(uint32_t t,uint16_t p,uint16_t s,uint16_t f) { input_config(t,2,p,s,f); }
void sh03_timer_input4(uint32_t t,uint16_t p,uint16_t s,uint16_t f) { input_config(t,3,p,s,f); }

static void input_prescaler(uint32_t timer,unsigned ch,uint16_t prescaler)
{
    uint32_t reg=timer+0x18+(ch/2)*4;
    unsigned shift=(ch%2)*8;
    SH03_U16(reg)&=~(12u<<shift);
    SH03_U16(reg)|=(uint32_t)prescaler<<shift;
}
void sh03_timer_input1_prescaler(uint32_t t,uint16_t p) { input_prescaler(t,0,p); }
void sh03_timer_input2_prescaler(uint32_t t,uint16_t p) { input_prescaler(t,1,p); }
void sh03_timer_input3_prescaler(uint32_t t,uint16_t p) { input_prescaler(t,2,p); }
void sh03_timer_input4_prescaler(uint32_t t,uint16_t p) { input_prescaler(t,3,p); }
void sh03_timer_input_init(uint32_t timer,const Sh03TimerInput *c)
{
    unsigned ch=c->channel==0?0:c->channel==4?1:c->channel==8?2:3;
    input_config(timer,ch,c->polarity,c->selection,c->filter);
    input_prescaler(timer,ch,c->prescaler);
}

static void output_config(uint32_t timer,unsigned ch,const Sh03TimerOutput *c)
{
    unsigned shift=ch*4;
    SH03_U16(timer+0x20)&=~(1u<<shift);
    uint16_t ccer=SH03_U16(timer+0x20);
    uint16_t cr2=SH03_U16(timer+4);
    uint32_t reg=timer+0x18+(ch/2)*4;
    uint16_t ccmr=SH03_U16(reg);
    ccer=(ccer&~(2u<<shift))|
                         ((uint32_t)c->polarity<<shift)|((uint32_t)c->output_enabled<<shift);
    if (ch==3) {
        if (advanced_timer(timer)) cr2=(cr2&0xbfff)|((uint32_t)c->idle<<6);
    } else if ((ch==0 && repetition_timer(timer)) || (ch!=0 && advanced_timer(timer))) {
        ccer=(ccer&~(8u<<shift))|((uint32_t)c->complementary_polarity<<shift);
        ccer=(ccer&~(4u<<shift))|((uint32_t)c->complementary_enabled<<shift);
        cr2=(cr2&~(0x300u<<(ch*2)))|((uint32_t)c->idle<<(ch*2))|((uint32_t)c->complementary_idle<<(ch*2));
    }
    SH03_U16(timer+4)=cr2;
    unsigned mode_shift=(ch%2)*8;
    SH03_U16(reg)=(ccmr&~(0x73u<<mode_shift))|((uint32_t)c->mode<<mode_shift);
    SH03_U16(timer+0x34+ch*4)=c->pulse;
    SH03_U16(timer+0x20)=ccer;
}
void sh03_timer_output1_init(uint32_t t,const Sh03TimerOutput *c) { output_config(t,0,c); }
void sh03_timer_output2_init(uint32_t t,const Sh03TimerOutput *c) { output_config(t,1,c); }
void sh03_timer_output3_init(uint32_t t,const Sh03TimerOutput *c) { output_config(t,2,c); }
void sh03_timer_output4_init(uint32_t t,const Sh03TimerOutput *c) { output_config(t,3,c); }
static void output_preload(uint32_t timer,unsigned ch,uint16_t value)
{
    uint32_t reg=timer+0x18+(ch/2)*4;
    unsigned shift=(ch%2)*8;
    SH03_U16(reg)=(SH03_U16(reg)&~(8u<<shift))|((uint32_t)value<<shift);
}
void sh03_timer_output1_preload(uint32_t t,uint16_t p) { output_preload(t,0,p); }
void sh03_timer_output2_preload(uint32_t t,uint16_t p) { output_preload(t,1,p); }
void sh03_timer_output3_preload(uint32_t t,uint16_t p) { output_preload(t,2,p); }
void sh03_timer_output4_preload(uint32_t t,uint16_t p) { output_preload(t,3,p); }
void sh03_timer_compare1(uint32_t t,uint16_t value) { SH03_U16(t+0x34)=value; }
void sh03_timer_compare2(uint32_t t,uint16_t value) { SH03_U16(t+0x38)=value; }

static void update(uint32_t reg,uint16_t mask,uint8_t enabled)
{
    if (enabled) SH03_U16(reg)|=mask; else SH03_U16(reg)&=~mask;
}
void sh03_timer_preload(uint32_t timer,uint8_t enabled) { update(timer,0x80,enabled); }
void sh03_timer_enable(uint32_t timer,uint8_t enabled) { update(timer,1,enabled); }
void sh03_timer_pwm_outputs(uint32_t timer,uint8_t enabled) { update(timer+0x44,0x8000,enabled); }
void sh03_timer_dma(uint32_t timer,uint16_t request,uint8_t enabled) { update(timer+0xc,request,enabled); }
void sh03_timer_base_init(uint32_t timer,const Sh03TimerBase *c)
{
    uint16_t cr1=SH03_U16(timer);
    if (full_timer(timer)) cr1=(cr1&0xff8f)|c->counter_mode;
    if (timer!=0x40001000 && timer!=0x40001400) cr1=(cr1&0xfcff)|c->clock_division;
    SH03_U16(timer)=cr1;
    SH03_U16(timer+0x2c)=c->period;
    SH03_U16(timer+0x28)=c->prescaler;
    if (repetition_timer(timer)) SH03_U16(timer+0x30)=c->repetition;
    SH03_U16(timer+0x14)=1;
}
