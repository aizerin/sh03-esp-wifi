#ifndef SH03_DISPLAY_H
#define SH03_DISPLAY_H
#include "sh03_ui.h"

typedef struct {
    uint32_t clock_mask, port;
    uint16_t pin_mask;
    uint8_t pin_number, reserved;
} Sh03Gpio;
typedef struct { Sh03Gpio select, write_clock, read_clock, data; } Sh03DisplayBus;
typedef struct { uint8_t address, layout, display; } Sh03DisplaySegment;
typedef struct { uint8_t address_a, data_a, address_b, data_b; } Sh03DisplayGlyph;
_Static_assert(sizeof(Sh03Gpio)==12,"GPIO descriptor ABI");
_Static_assert(sizeof(Sh03DisplayBus)==48,"display bus ABI");
_Static_assert(sizeof(Sh03DisplaySegment)==3,"segment descriptor ABI");

#define sh03_display_buses ((const Sh03DisplayBus *)0x08010db4)
#define sh03_display_segment(ch,offset) ((const Sh03DisplaySegment *)(0x08010e14 + 60u*(ch) + (offset)))
#define sh03_display_previous ((Sh03ChamberUi *)0x20000394)
#define fw_display_select SH03_FN(0x08001a10,void,const Sh03DisplayBus *,uint8_t)
#define fw_display_read_data SH03_FN(0x08002640,uint8_t,const Sh03DisplayBus *)
#define fw_display_data_input SH03_FN(0x0800266c,void,const Sh03DisplayBus *)
#define fw_display_data SH03_FN(0x08002684,void,const Sh03DisplayBus *,uint8_t)
#define fw_display_data_output SH03_FN(0x080026b8,void,const Sh03DisplayBus *)
#define fw_display_write_clock SH03_FN(0x0800b224,void,const Sh03DisplayBus *,uint8_t)
#define fw_display_read_clock SH03_FN(0x08005cfc,void,const Sh03DisplayBus *,uint8_t)
#define fw_display_shift_in SH03_FN(0x08005d30,uint8_t,const Sh03DisplayBus *,uint8_t)
#define fw_display_shift_out SH03_FN(0x080066f4,void,const Sh03DisplayBus *,uint8_t,uint8_t)
#define fw_display_command SH03_FN(0x08006784,void,const Sh03DisplayBus *,uint8_t)
#define fw_display_delay SH03_FN(0x08006a4c,void,uint16_t)
#define fw_display_bus_initialize SH03_FN(0x0800b4f0,void,const Sh03DisplayBus *)
#define fw_display_buses_initialize SH03_FN(0x080078e8,void,const Sh03DisplayBus *,uint8_t)
#define fw_gpio_descriptor_configure SH03_FN(0x08003b94,void,const Sh03Gpio *,uint8_t,uint8_t)
#define fw_gpio_configure SH03_FN(0x08003868,void,uint32_t,uint16_t,uint8_t,uint8_t)
#define fw_gpio_remap SH03_FN(0x08003a84,void,uint32_t,uint32_t)
#define fw_apb2_clock SH03_FN(0x08005b18,void,uint32_t,uint32_t)

#define fw_display_write SH03_FN(0x08006940,void,uint8_t,uint8_t,uint8_t)
#define fw_display_read SH03_FN(0x08005db8,uint8_t,uint8_t,uint8_t)
#define fw_display_icon SH03_FN(0x08004304,void,const Sh03DisplaySegment *,uint8_t)
#define fw_display_digit SH03_FN(0x08004198,void,uint8_t,const Sh03DisplaySegment *,uint8_t,uint8_t)
#define fw_display_letter SH03_FN(0x08004378,void,uint8_t,const Sh03DisplaySegment *,uint8_t,uint8_t)
#define fw_display_digit_glyph SH03_FN(0x080054e0,void,const Sh03DisplaySegment *,uint8_t,uint8_t,Sh03DisplayGlyph *)
#define fw_display_letter_glyph SH03_FN(0x0800b254,void,const Sh03DisplaySegment *,uint8_t,uint8_t,Sh03DisplayGlyph *)
#define fw_display_letter_font SH03_FN(0x0800dce0,uint8_t,uint8_t,uint8_t)
#define fw_display_chamber SH03_FN(0x08002c38,void,uint8_t,const Sh03ChamberUi *,uint8_t)
#define fw_display_measured_temperature SH03_FN(0x08002cdc,void,uint8_t,uint8_t,uint8_t)
#define fw_display_units SH03_FN(0x08003008,void,uint8_t,uint8_t)
#define fw_display_error SH03_FN(0x08003080,void,uint8_t,const char *,uint8_t)
#define fw_display_humidity_icon SH03_FN(0x08002c0c,void,uint8_t,uint8_t)
#define fw_display_timer_icon SH03_FN(0x0800321c,void,uint8_t,uint8_t)
#define fw_display_running_icon SH03_FN(0x0800324c,void,uint8_t,uint8_t)
#define fw_display_boot_segment SH03_FN(0x08002e74,void,uint8_t,uint8_t)
#define fw_display_power_icons SH03_FN(0x08002db8,void,uint8_t)
#define fw_boot_beep SH03_FN(0x080050a4,void,void)
#define fw_busy_delay SH03_FN(0x0800ac44,void,uint16_t)

#ifdef SH03_CUSTOM_UART
/* Global LCD and backlight gate. RAM/rendering remain live while dark.
 * Runtime setters are serialized by the UI mutex. Default after reset is on;
 * there is no inactivity timeout. A first touch wakes without a key action. */
uint8_t sh03_display_enabled(void);
void sh03_display_set_enabled(uint8_t enabled);
#endif

static inline uintptr_t sh03_gpio_alias(const Sh03Gpio *gpio,uint32_t offset)
{
    uint32_t reg=gpio->port+offset;
    return (reg&0xf0000000u)+((reg&0xfffffu)<<5)+gpio->pin_number*4u+0x02000000u;
}
#endif
