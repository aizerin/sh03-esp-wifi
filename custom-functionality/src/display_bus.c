/* Serial display transport reconstructed from the original GPIO transitions.
 * The interface has independent /WR and /RD clocks and bidirectional data. */
#include "sh03_display.h"

#ifdef SH03_CUSTOM_UART
static uint8_t displays_off;

uint8_t sh03_display_enabled(void) { return !displays_off; }

void sh03_display_set_enabled(uint8_t enabled)
{
    uint8_t off=enabled==0;
    if (displays_off==off) return;
    displays_off=off;
    /* PB1 is the common backlight gate, physically confirmed on this board.
     * The original setter is inverted: 1 writes LOW (dark), 0 writes HIGH.
     * Hide before LCD OFF; expose only after both LCD outputs are enabled.
     * Its bit-band write changes PB1 alone, preserving all other GPIO bits. */
    if (off) SH03_FN(0x08002580,void,uint8_t)(1);
    /* HT1621-compatible command framing, already used by the original init:
     * prefix 1000 + 0x06 = LCD ON (100 00000011 0); 0x04 = LCD OFF.
     * Leave SYS EN and display RAM intact, so data and glyph caches stay live. */
    for (uint8_t ch=0;ch<2;++ch)
        fw_display_command(&sh03_display_buses[ch],off?0x04:0x06);
    if (!off) SH03_FN(0x08002580,void,uint8_t)(0);
}
#endif

void sh03_display_select(const Sh03DisplayBus *bus,uint8_t value) { SH03_U32(sh03_gpio_alias(&bus->select,12))=value; }
void sh03_display_write_clock(const Sh03DisplayBus *bus,uint8_t value) { SH03_U32(sh03_gpio_alias(&bus->write_clock,12))=value; }
void sh03_display_read_clock(const Sh03DisplayBus *bus,uint8_t value) { SH03_U32(sh03_gpio_alias(&bus->read_clock,12))=value; }
void sh03_display_data(const Sh03DisplayBus *bus,uint8_t value) { SH03_U32(sh03_gpio_alias(&bus->data,12))=value; }
uint8_t sh03_display_read_data(const Sh03DisplayBus *bus) { return (uint8_t)SH03_U32(sh03_gpio_alias(&bus->data,8)); }
void sh03_display_data_input(const Sh03DisplayBus *bus) { fw_gpio_descriptor_configure(&bus->data,4,0); }
void sh03_display_data_output(const Sh03DisplayBus *bus) { fw_gpio_descriptor_configure(&bus->data,0x14,1); }
void sh03_gpio_descriptor_configure(const Sh03Gpio *gpio,uint8_t mode,uint8_t speed) { fw_gpio_configure(gpio->port,gpio->pin_mask,mode,speed); }

/* Keep the original delay instruction sequence. A compiler-eliminated or
 * shortened empty C loop would change the serial interface's hold times. */
__attribute__((naked))
void sh03_display_delay(uint16_t units __attribute__((unused)))
{
    __asm volatile("sub sp, #4\n strh.w r0, [sp, #2]\n b 1f\n"
                   "1: ldrh.w r0, [sp, #2]\n cbz r0, 3f\n b 2f\n"
                   "2: ldrh.w r0, [sp, #2]\n subs r0, #1\n strh.w r0, [sp, #2]\n b 1b\n"
                   "3: add sp, #4\n bx lr\n");
}

void sh03_display_shift_out(const Sh03DisplayBus *bus,uint8_t bits,uint8_t count)
{
    fw_display_data_output(bus);
    for (uint8_t i=0;i<count;++i) {
        fw_display_write_clock(bus,0);
        fw_display_data(bus,(bits&0x80)!=0);
        fw_display_delay(1);
        fw_display_write_clock(bus,1);
        bits<<=1;
    }
    fw_display_write_clock(bus,1);
    fw_display_data(bus,1);
}

uint8_t sh03_display_shift_in(const Sh03DisplayBus *bus,uint8_t count)
{
    fw_display_data_input(bus);
    uint8_t value=0;
    for (uint8_t i=0;i<count;++i) {
        fw_display_read_clock(bus,0);
        fw_display_delay(1);
        fw_display_read_clock(bus,1);
        fw_display_delay(1);
        if (fw_display_read_data(bus)!=0 && i<32) value|=(uint8_t)(8u>>i);
    }
    fw_display_read_clock(bus,1);
    return value;
}

void sh03_display_write_nibble(uint8_t display,uint8_t address,uint8_t data)
{
    const Sh03DisplayBus *bus=&sh03_display_buses[display];
    fw_display_select(bus,0);
    fw_display_delay(1);
    fw_display_shift_out(bus,0xa0,3);
    fw_display_shift_out(bus,(uint8_t)(address<<2),6);
    fw_display_shift_out(bus,(uint8_t)(data<<4),4);
    fw_display_select(bus,1);
    fw_display_delay(1);
}

uint8_t sh03_display_read_nibble(uint8_t display,uint8_t address)
{
    const Sh03DisplayBus *bus=&sh03_display_buses[display];
    fw_display_select(bus,0);
    fw_display_delay(1);
    fw_display_shift_out(bus,0xc0,3);
    fw_display_shift_out(bus,(uint8_t)(address<<2),6);
    uint8_t result=fw_display_shift_in(bus,4);
    fw_display_select(bus,1);
    fw_display_delay(1);
    return result;
}

void sh03_display_command(const Sh03DisplayBus *bus,uint8_t command)
{
#ifdef SH03_CUSTOM_UART
    /* A bus reinitialization must respect the user's global output gate. */
    if (displays_off && command==0x06) command=0x04;
#endif
    fw_display_select(bus,0);
    fw_display_delay(1);
    fw_display_shift_out(bus,0x80,4);
    fw_display_shift_out(bus,command,8);
    fw_display_select(bus,1);
    fw_display_delay(1);
}

void sh03_display_bus_initialize(const Sh03DisplayBus *bus)
{
    fw_display_select(bus,1);
    fw_display_data(bus,1);
    fw_display_write_clock(bus,1);
    fw_display_read_clock(bus,1);
    fw_display_delay(5);
    const uint8_t commands[]={0x52,0x30,0,10,2,6,0x10};
    for (unsigned i=0;i<sizeof commands;++i) fw_display_command(bus,commands[i]);
    fw_display_delay(1);
}

void sh03_display_buses_initialize(const Sh03DisplayBus *buses,uint8_t count)
{
    fw_apb2_clock(1,1);
    fw_gpio_remap(0x300200,1);
    for (uint8_t i=0;i<count;++i) {
        const Sh03DisplayBus *bus=&buses[i];
        fw_apb2_clock(bus->select.clock_mask|bus->read_clock.clock_mask|bus->write_clock.clock_mask|bus->data.clock_mask,1);
        fw_gpio_configure(bus->select.port,bus->select.pin_mask,0x14,1);
        fw_gpio_configure(bus->write_clock.port,bus->write_clock.pin_mask,0x14,1);
        fw_gpio_configure(bus->read_clock.port,bus->read_clock.pin_mask,0x14,1);
        fw_gpio_configure(bus->data.port,bus->data.pin_mask,0x14,1);
        fw_display_bus_initialize(bus);
        fw_display_delay(2);
    }
}
void sh03_display_hardware_init(void) { fw_display_buses_initialize(sh03_display_buses,2); }
