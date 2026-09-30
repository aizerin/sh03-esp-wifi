#include "sh03_peripherals.h"

static void gxht_bus_failure(uint8_t chamber)
{
    if (SH03_U8(0x200006d2+chamber)<11) {
        ++SH03_U8(0x200006d2+chamber);
        fw_event_set(sh03_events(chamber),0x10);
    } else if (fw_fault(chamber)==10) fw_event_set(sh03_events(chamber),0x40);
}

int sh03_gxht30_read(uint8_t chamber,float *temperature,uint32_t *humidity)
{
    uint8_t data[6]={0};
    if (fw_event_get(sh03_events(chamber))&0x10) return 0;
    uint8_t address=SH03_U8(0x200000ec+chamber);
    if (fw_i2c_write(sh03_i2c_bus,address,(void *)0x2000029c,2,20)!=0) { gxht_bus_failure(chamber);return 0; }
    fw_busy_delay(5);
    if (fw_i2c_read(sh03_i2c_bus,address,data,6,20)!=0) { gxht_bus_failure(chamber);return 0; }
    SH03_U8(0x200006d2+chamber)=0;
    uint8_t temp_crc=fw_sensor_crc(data,2),humidity_crc=fw_sensor_crc(data+3,2);
    if (temp_crc!=data[2] || humidity_crc!=data[5]) {
        fw_printf("Chamber:%d crc error\r\n",chamber,temp_crc,humidity_crc);
        fw_i2c_write(sh03_i2c_bus,address,(void *)0x2000029e,2,20);
        if (fw_i2c_write(sh03_i2c_bus,address,(void *)0x200002a0,2,20)==1) fw_printf("Chamber%d GXHT30 I2C reset successed.\r\n",chamber);
        return 0;
    }
    /* Preserve the original zero check, including its use of the temperature
     * CRC byte (data[2]) in the second condition. */
    if ((data[0]==0 && data[1]==0) || (data[2]==0 && data[3]==0)) { *temperature=0;*humidity=0;return 1; }
    uint32_t raw_temperature=(data[0]<<8)|data[1];
    float value=((float)raw_temperature*175.0f)/65535.0f+-45.0f;
    *temperature=value<0?0:value>99?99:value;
    uint32_t raw_humidity=(data[3]<<8)|data[4];
    uint32_t rh=(raw_humidity*1000/65535+5)/10;
    *humidity=rh<100?rh:99;
    return 1;
}

void sh03_sensors_initialize(void)
{
    for (uint8_t ch=0;ch<2;++ch) {
        fw_i2c_write(sh03_i2c_bus,SH03_U8(0x200000ec+ch),(void *)0x200002a0,2,10);
        fw_busy_delay_us(10);
    }
}

uint8_t sh03_touch_read_code(void)
{
    if (fw_i2c_read(sh03_i2c_bus,SH03_U8(0x20000258),(void *)0x2000025a,1,5)!=0) {
        fw_printf("XW05A_Get_TouchCode: i2c read failed\r\n");return 0;
    }
    switch (SH03_U8(0x2000025a)) {
    case 0xe7:case 0xef:return 1;
    case 0xf7:return 2;
    case 0xfb:return 3;
    case 0xf9:case 0xfd:return 4;
    case 0xfc:case 0xfe:return 5;
    case 0xf8:case 0xfa:return 6;
    default:return 0;
    }
}

uint8_t sh03_hall_read(uint8_t chamber,uint16_t *samples)
{
    uint32_t started=fw_ticks();
    uint32_t dma=SH03_U32(0x2000016c),adc=SH03_U32(0x20000160);
    fw_dma_set_count(dma,4);
    fw_dma_enable(dma,1);
    fw_adc_start(adc,1);
    while (fw_dma_count(dma)!=0 && fw_dma_flag(0x10020000)==0) {
        if (fw_ticks()-started>50) { fw_printf("Get chamber%d hall adc timeout.\r\n",chamber+1);return 0; }
    }
    fw_dma_clear_flag(0x10020000);
    fw_dma_enable(dma,0);
    fw_adc_start(adc,0);
    for (unsigned i=0;i<4;++i) samples[i]=SH03_U16(0x200006ca+i*2);
    return 1;
}

uint8_t sh03_fan_capture(uint8_t chamber)
{
    uint32_t started=fw_ticks();
    uint32_t dma=SH03_U32(0x20000068+chamber*80u),timer=SH03_U32(0x20000060+chamber*80u);
    SH03_U32(dma)|=1;
    uint16_t request=chamber==0?0x200:0x400;
    SH03_U16(timer+12)|=request;
    fw_timer_dma(timer,request,1);
    while (SH03_U32(dma+4)!=0) {
        if (fw_ticks()-started>500) { fw_printf("Chamber%d Fan FG detect error\r\n",chamber+1);return 0; }
        fw_delay(10);
    }
    SH03_U16(timer+12)&=(uint16_t)~request;
    SH03_U32(dma)&=0xfffe;
    SH03_U32(dma+4)=9;
    uint32_t samples=SH03_U32(0x2000007c+chamber*80u),sum=0;
    for (unsigned i=1;i<9;++i) sum+=(uint16_t)(SH03_U16(samples+i*2)-SH03_U16(samples+(i-1)*2));
    uint32_t denominator=(uint16_t)(sum>>3)*2u;
    /* Original UDIV returns zero for a zero denominator (DIV_0_TRP unset). */
    uint32_t rpm=denominator?60000000u/denominator:0;
    SH03_U16(0x20000078+chamber*80u)=(uint16_t)rpm;
    for (unsigned i=0;i<9;++i) SH03_U16(samples+i*2)=0;
    return 1;
}
uint32_t sh03_fan_read_rpm(uint8_t chamber)
{
    if (SH03_U8(0x20000080+chamber*80u)==1) fw_fan_capture(chamber);
    return SH03_U16(0x20000078+chamber*80u);
}
