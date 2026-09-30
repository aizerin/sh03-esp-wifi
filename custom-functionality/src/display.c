/* Segment encoding and renderer recovered from SH03 V3.5.1. Layout tables in
 * original flash identify address, orientation and display for each element. */
#include "sh03_display.h"

void sh03_display_digit_glyph(const Sh03DisplaySegment *segment,uint8_t digit,uint8_t visible,Sh03DisplayGlyph *out)
{
    uint8_t shift=((const uint8_t *)0x08010e97)[segment->layout];
    out->address_a=segment->address%40;
    out->address_b=out->address_a+1;
    uint8_t glyph=0;
    if (visible==1 && digit<10) glyph=((const uint8_t *)0x08010e37)[segment->layout*11u+digit];
    else if (visible==2) glyph=((const uint8_t *)0x08010e41)[segment->layout*11u];
    uint8_t high=fw_display_read(segment->display,segment->address);
    uint8_t low=fw_display_read(segment->display,segment->address+1);
    uint8_t mask=shift<32?(uint8_t)(1u<<shift):0;
    glyph|=((high<<4)|low)&mask;
    out->data_a=glyph>>4;
    out->data_b=glyph&15;
}

uint8_t sh03_display_letter_font(uint8_t letter,uint8_t layout)
{
    static const char letters[]="ABCEGLPSTUVFNH";
    for (unsigned i=0;i<sizeof letters-1;++i) {
        if (letter==(uint8_t)letters[i]) return ((const uint8_t *)0x08010e45)[layout*14u+i];
    }
    return 0;
}

void sh03_display_letter_glyph(const Sh03DisplaySegment *segment,uint8_t letter,uint8_t visible,Sh03DisplayGlyph *out)
{
    uint8_t shift=((const uint8_t *)0x08010e97)[segment->layout];
    out->address_a=segment->address%40;
    out->address_b=out->address_a+1;
    uint8_t glyph=0;
    if (visible==1) glyph=fw_display_letter_font(letter,segment->layout);
    else if (visible==2) glyph=((const uint8_t *)0x08010e41)[segment->layout*11u];
    uint8_t high=fw_display_read(segment->display,segment->address);
    uint8_t low=fw_display_read(segment->display,segment->address+1);
    uint8_t mask=shift<32?(uint8_t)(1u<<shift):0;
    glyph|=((high<<4)|low)&mask;
    out->data_a=glyph>>4;
    out->data_b=glyph&15;
}

void sh03_display_digit(uint8_t chamber,const Sh03DisplaySegment *segment,uint8_t digit,uint8_t visible)
{
    (void)chamber;
    Sh03DisplayGlyph glyph={0};
    fw_display_digit_glyph(segment,digit,visible,&glyph);
    fw_display_write(segment->display,glyph.address_a,glyph.data_a);
    if (segment->display==0 && segment->address==26) {
        /* Four segments of the last time digit share nibbles with other
         * symbols, instead of occupying the nominal second nibble. */
        static const uint8_t addresses[]={22,20,14,12};
        for (unsigned i=0;i<4;++i) {
            uint8_t data=visible==0?0:(uint8_t)((glyph.data_b<<(i))&8);
            data|=fw_display_read(segment->display,addresses[i])&7;
            fw_display_write(segment->display,addresses[i],data);
        }
    } else fw_display_write(segment->display,glyph.address_b,glyph.data_b);
}

void sh03_display_letter(uint8_t chamber,const Sh03DisplaySegment *segment,uint8_t letter,uint8_t visible)
{
    (void)chamber;
    Sh03DisplayGlyph glyph={0};
    fw_display_letter_glyph(segment,letter,visible,&glyph);
    fw_display_write(segment->display,glyph.address_a,glyph.data_a);
    fw_display_write(segment->display,glyph.address_b,glyph.data_b);
}

void sh03_display_icon(const Sh03DisplaySegment *segment,uint8_t enabled)
{
    uint8_t shift=segment->layout-1;
    uint8_t mask=shift<32?(uint8_t)(8u>>shift):0;
    uint8_t old=fw_display_read(segment->display,segment->address);
    uint8_t data=enabled==0?(old&~mask):(mask|(old&15));
    fw_display_write(segment->display,segment->address,data);
}

static void display_pair(uint8_t chamber,uint8_t value,uint8_t visible,unsigned offset)
{
    fw_display_digit(chamber,sh03_display_segment(chamber,offset),value/10,visible);
    fw_display_digit(chamber,sh03_display_segment(chamber,offset+3),value%10,visible);
}
void sh03_display_humidity(uint8_t chamber,uint8_t value,uint8_t visible) { display_pair(chamber,value,visible,3); }
void sh03_display_temperature(uint8_t chamber,uint8_t value,uint8_t visible) { display_pair(chamber,value,visible,24); }
void sh03_display_measured_temperature(uint8_t chamber,uint8_t value,uint8_t visible) { display_pair(chamber,value,visible,33); }

void sh03_display_time(uint8_t chamber,uint8_t hours,uint8_t minutes,uint8_t visible)
{
    const uint8_t digits[]={hours/10,hours%10,minutes/10,minutes%10};
    for (unsigned i=0;i<4;++i) fw_display_digit(chamber,sh03_display_segment(chamber,42+i*3),digits[i],visible);
}

void sh03_display_material(uint8_t chamber,uint8_t material,uint8_t visible)
{
    const char *name=sh03_materials[material].name;
    unsigned length=0;
    while (name[length]!=0) ++length;
    length=(uint8_t)length;
    uint8_t letter=0;
    for (unsigned i=0;i<4;++i) {
        uint8_t show=0;
        if ((int)i >= 4-(int)length) { letter=(uint8_t)name[length+i-4]; show=visible; }
        /* Original leaves 'letter' uninitialized for a leading blank. The
         * glyph routine ignores it when show==0; initialize it explicitly. */
        fw_display_letter(chamber,sh03_display_segment(chamber,12+i*3),letter,show);
    }
}

void sh03_display_error(uint8_t chamber,const char *code,uint8_t visible)
{
    fw_display_letter(chamber,sh03_display_segment(chamber,15),(uint8_t)code[0],visible);
    fw_display_digit(chamber,sh03_display_segment(chamber,18),(uint8_t)(code[1]-'0'),visible);
}

void sh03_display_units(uint8_t chamber,uint8_t visible)
{
    fw_display_icon(sh03_display_segment(chamber,9),visible);
    fw_display_icon(sh03_display_segment(chamber,30),visible);
    fw_display_icon(sh03_display_segment(chamber,39),visible);
    fw_display_icon(sh03_display_segment(chamber,54),visible);
}
void sh03_display_humidity_icon(uint8_t chamber,uint8_t value) { fw_display_icon(sh03_display_segment(chamber,0),value); }
void sh03_display_timer_icon(uint8_t chamber,uint8_t value) { fw_display_icon(sh03_display_segment(chamber,54),value); }
void sh03_display_running_icon(uint8_t chamber,uint8_t value) { fw_display_icon(sh03_display_segment(chamber,57),value); }
void sh03_display_boot_segment(uint8_t index,uint8_t value) { fw_display_icon((const Sh03DisplaySegment *)(0x08010e92+index*3u),value); }
void sh03_display_power_icons(uint8_t value)
{
    fw_display_icon((const Sh03DisplaySegment *)0x08010e8c,value);
    fw_display_icon((const Sh03DisplaySegment *)0x08010e8f,value);
}

void sh03_display_chamber(uint8_t chamber,const Sh03ChamberUi *state,uint8_t visible)
{
    fw_display_units(chamber,visible);
    fw_display_humidity(chamber,state->humidity,visible);
    fw_display_material(chamber,state->material,visible);
    fw_display_temperature(chamber,state->target_temperature,visible);
    fw_display_measured_temperature(chamber,state->temperature,visible);
    fw_display_time(chamber,(uint8_t)(state->duration_seconds/3600),(uint8_t)((state->duration_seconds/60)%60),visible);
}

void sh03_display_clear_all(void)
{
    for (uint8_t address=0;address<32;++address) {
        fw_display_write(0,address,0);
        fw_display_write(1,address,0);
    }
}
void sh03_display_boot_animation(void)
{
    for (uint8_t i=0;i<5;++i) { fw_display_boot_segment(i,1); fw_boot_beep(); fw_busy_delay(65); }
    fw_display_power_icons(1); fw_boot_beep(); fw_busy_delay(90);
}

void sh03_display_render(const void *system_state,const Sh03Ui *ui,uint8_t event)
{
    const uint8_t *system=system_state;
    uint8_t setting=system[0];
    for (uint8_t ch=0;ch<2;++ch) {
        const Sh03ChamberUi *s=&ui->chambers[ch];
        Sh03ChamberUi *previous=&sh03_display_previous[ch];
        uint8_t fault=system[20+ch*2];
        uint8_t visible=s->selected!=0;
        if (s->fault_shown==1 && fault!=6 && fault!=7) {
            if (previous->selected!=s->selected) {
                fw_display_chamber(ch,s,visible);
                fw_display_icon(sh03_display_segment(ch,0),visible);
                fw_display_icon(sh03_display_segment(ch,57),visible);
                previous->selected=s->selected;
            }
            fw_display_error(ch,(const char *)(0x08010b88+fault*3u),ui->blink);
            continue;
        }
        if (event!=0) {
            if ((int)(event>>4)-1==ch) {
                if ((event&15)==0) fw_display_setting(s,ch,1);
                else fw_display_chamber(ch,s,1);
            } else if (event==0x31) fw_display_chamber(ch,s,1);
        }
        if (fault==6 || fault==7) fw_display_error(ch,(const char *)(0x08010b88+fault*3u),ui->blink);
        uint8_t blink=visible?ui->blink:visible;
        if (previous->selected==s->selected) {
            if (ch==ui->chamber && setting==1) fw_display_chamber(ch,s,blink);
            else {
                if (ch==ui->chamber && setting==2) {
                    fw_display_humidity(ch,s->humidity,s->setting_item==0?blink:visible);
                    fw_display_material(ch,s->material,s->setting_item==1?blink:visible);
                    fw_display_temperature(ch,s->target_temperature,s->setting_item==2?blink:visible);
                    fw_display_time(ch,(uint8_t)(s->duration_seconds/3600),(uint8_t)((s->duration_seconds/60)%60),s->setting_item==3?blink:visible);
                } else {
                    if (previous->material!=s->material) fw_display_material(ch,s->material,visible);
                    if (previous->target_temperature!=s->target_temperature) fw_display_temperature(ch,s->target_temperature,visible);
                    uint32_t bits=fw_event_get(sh03_events(ch));
                    if ((bits&0x80) || system[4+ch*8]==1 || previous->duration_seconds!=s->duration_seconds) {
                        fw_event_get(sh03_events(ch));
                        uint8_t show=system[4+ch*8]==1 && s->duration_seconds==7200?2:visible;
                        fw_display_time(ch,(uint8_t)(s->duration_seconds/3600),(uint8_t)((s->duration_seconds/60)%60),show);
                    }
                }
                if (previous->humidity!=s->humidity) fw_display_humidity(ch,s->humidity,visible);
                if (previous->temperature!=s->temperature) fw_display_measured_temperature(ch,s->temperature,visible);
                previous->running_icon=s->running_icon;
                previous->material=s->material;
                previous->target_temperature=s->target_temperature;
                previous->duration_seconds=s->duration_seconds;
                previous->humidity=s->humidity;
                previous->temperature=s->temperature;
            }
        } else {
            fw_display_chamber(ch,s,s->selected==1);
            previous->selected=s->selected;
        }
        if (previous->humidity_icon!=s->humidity_icon) {
            fw_display_humidity_icon(ch,s->humidity_icon==1);
            previous->humidity_icon=s->humidity_icon;
        }
        fw_display_running_icon(ch,s->running_icon==1);
        if (ch==ui->chamber && system[0]==2) blink=visible;
        fw_display_timer_icon(ch,blink);
    }
}
