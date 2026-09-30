/* Small accessors used across task boundaries; all offsets match original RAM. */
#include "sh03_runtime.h"

uint8_t sh03_get_fan_enabled(uint8_t ch) { return SH03_U8(0x20000080 + ch * 0x50u); }
uint32_t sh03_get_humidity_elapsed(uint8_t ch) { return sh03_humidity_elapsed(ch); }
uint8_t sh03_get_mode(uint8_t ch) { return sh03_mode(ch); }
uint8_t sh03_get_target_temperature(uint8_t ch) { return sh03_messages[ch].target_temperature; }
uint8_t sh03_get_ui_running(uint8_t ch) { return sh03_ui_running(ch); }
uint8_t sh03_get_fault(uint8_t ch) { return SH03_U8(0x2000002c + 2u * ch); }
uint8_t sh03_get_running(uint8_t ch) { return sh03_controls[ch].running; }
uint8_t sh03_get_band(uint8_t ch) { return sh03_controls[ch].temperature_band; }
uint32_t sh03_get_elapsed(uint8_t ch) { return sh03_elapsed(ch); }
uint16_t sh03_get_ntc_adc(uint8_t ch) { return SH03_MEM(uint16_t, 0x200006d4 + 2u * ch); }
uint8_t sh03_get_diagnostic(void) { return sh03_diagnostic; }
void sh03_set_diagnostic(uint8_t enabled) { sh03_diagnostic = enabled; }
