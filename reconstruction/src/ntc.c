/* NTC transfer table: 220 descending ADC codes for -20..199 degrees Celsius.
 * The original search at 0x0800512c seeded its midpoint from uninitialized
 * stack memory and used temperature==0 as a loop sentinel. This reconstruction
 * intentionally fixes those two defects; see docs/limitations.md and the
 * exhaustive test report. Endpoint saturation remains the original policy.
 */
#include "sh03_peripherals.h"

int32_t sh03_ntc_adc_to_temperature(uint16_t adc)
{
    const uint16_t *table=(const uint16_t *)0x08010bfc;
    if (adc>=4044) return -20;
    if (adc<227) return 199;
    unsigned low=0,high=219;
    while (low<high) {
        unsigned middle=(low+high+1)/2;
        if (adc>table[middle]) high=middle-1;
        else low=middle;
    }
    return (int32_t)low-20;
}
int32_t sh03_ntc_read_temperature(uint8_t chamber)
{
    uint16_t adc=SH03_FN(0x080045a0,uint16_t,uint8_t)(chamber);
    return SH03_FN(0x0800512c,int32_t,uint16_t)(adc);
}
