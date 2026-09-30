#ifndef SH03_PERIPHERALS_H
#define SH03_PERIPHERALS_H
#include "sh03_display.h"

#define SH03_U16(address) SH03_MEM(uint16_t,address)
typedef struct { uint32_t peripheral,memory,direction,count,peripheral_increment,memory_increment,peripheral_width,memory_width,mode,priority,memory_to_memory; } Sh03DmaConfig;
typedef struct { uint32_t clock_hz; uint16_t mode,duty,own_address,ack,address_mode,reserved; } Sh03I2cConfig;
typedef struct { uint8_t index,reserved[3]; uint32_t peripheral,tx_dma,rx_dma,reserved_10; void *completion; } Sh03I2cBus;
typedef struct { uint32_t sysclk,hclk,pclk1,pclk2,adcclk; } Sh03ClockFrequencies;
typedef struct { uint16_t prescaler,counter_mode,period,clock_division; uint8_t repetition,reserved; } Sh03TimerBase;
typedef struct { uint16_t mode,output_enabled,complementary_enabled,pulse,polarity,complementary_polarity,idle,complementary_idle; } Sh03TimerOutput;
typedef struct { uint16_t channel,polarity,selection,prescaler,filter; } Sh03TimerInput;
typedef struct { uint8_t irq,preemption_priority,subpriority,enabled; } Sh03NvicConfig;
typedef struct { uint32_t baud; uint16_t word_length,stop_bits,parity,mode,flow_control,reserved; } Sh03UartConfig;
typedef struct { uint32_t mode; uint8_t scan,continuous,reserved[2]; uint32_t external_trigger,alignment; uint8_t channel_count,reserved_tail[3]; } Sh03AdcConfig;
typedef struct { uint16_t pins; uint8_t speed,mode; } Sh03GpioConfig;
typedef struct { uint32_t lines; uint8_t mode,trigger,enabled,reserved; } Sh03ExtiConfig;
_Static_assert(sizeof(Sh03DmaConfig)==44,"DMA config ABI");
_Static_assert(sizeof(Sh03I2cConfig)==16,"I2C config ABI");
_Static_assert(sizeof(Sh03I2cBus)==24,"I2C bus ABI");
#define sh03_i2c_bus ((Sh03I2cBus *)0x20000000)
#define fw_i2c_read SH03_FN(0x0800b930,uint8_t,const Sh03I2cBus *,uint8_t,void *,uint32_t,uint32_t)
#define fw_i2c_write SH03_FN(0x0800ba78,uint8_t,const Sh03I2cBus *,uint8_t,const void *,uint32_t,uint32_t)
#define fw_i2c_event SH03_FN(0x08004a74,int32_t,uint32_t,uint32_t)
#define fw_i2c_enable SH03_FN(0x08004ac8,void,uint32_t,uint8_t)
#define fw_i2c_dma SH03_FN(0x08004af4,void,uint32_t,uint8_t)
#define fw_i2c_last_transfer SH03_FN(0x08004b20,void,uint32_t,uint8_t)
#define fw_i2c_deinit SH03_FN(0x08004b4c,void,uint32_t)
#define fw_i2c_start SH03_FN(0x08004b94,void,uint32_t,uint8_t)
#define fw_i2c_stop SH03_FN(0x08004bc0,void,uint32_t,uint8_t)
#define fw_i2c_init SH03_FN(0x08004bec,void,uint32_t,const Sh03I2cConfig *)
#define fw_i2c_address SH03_FN(0x08004d98,void,uint32_t,uint8_t,uint8_t)
#define fw_apb1_clock SH03_FN(0x08005aa8,void,uint32_t,uint8_t)
#define fw_apb1_reset SH03_FN(0x08005ae0,void,uint32_t,uint8_t)
#define fw_clock_frequencies SH03_FN(0x08005b88,void,Sh03ClockFrequencies *)
#define fw_dma_clear_flag SH03_FN(0x080027fc,void,uint32_t)
#define fw_dma_clear_irq SH03_FN(0x0800282c,void,uint32_t)
#define fw_dma_enable SH03_FN(0x0800285c,void,uint32_t,uint8_t)
#define fw_dma_count SH03_FN(0x08002a44,uint16_t,uint32_t)
#define fw_dma_flag SH03_FN(0x08002a54,int32_t,uint32_t)
#define fw_dma_irq SH03_FN(0x08002aa8,int32_t,uint32_t)
#define fw_dma_set_count SH03_FN(0x08002b98,void,uint32_t,uint16_t)
#define fw_semaphore_give_isr SH03_FN(0x0800e52c,int32_t,void *,int32_t *)
#define fw_adc_start SH03_FN(0x080018bc,void,uint32_t,uint8_t)
#define fw_sensor_crc SH03_FN(0x08001a40,uint8_t,const uint8_t *,uint8_t)
#define fw_busy_delay_us SH03_FN(0x0800acb4,void,uint16_t)
#define fw_fan_capture SH03_FN(0x0800b560,uint8_t,uint8_t)
#define fw_timer_dma SH03_FN(0x08006ff8,void,uint32_t,uint16_t,uint8_t)
#define fw_timer_base SH03_FN(0x080077a4,void,uint32_t,const Sh03TimerBase *)
#define fw_timer_preload SH03_FN(0x08006f6c,void,uint32_t,uint8_t)
#define fw_timer_enable SH03_FN(0x08006f98,void,uint32_t,uint8_t)
#define fw_nvic_init SH03_FN(0x08005434,void,const Sh03NvicConfig *)
#define fw_dma_init SH03_FN(0x08002b2c,void,uint32_t,const Sh03DmaConfig *)
#define fw_dma_deinit SH03_FN(0x0800288c,void,uint32_t)
#define fw_dma_irq_config SH03_FN(0x08002afc,void,uint32_t,uint32_t,uint8_t)
#define fw_ahb_clock SH03_FN(0x08005a70,void,uint32_t,uint8_t)
#define fw_adc_init SH03_FN(0x08001690,void,uint32_t,const Sh03AdcConfig *)
#define fw_adc_channel SH03_FN(0x0800172c,void,uint32_t,uint8_t,uint8_t,uint8_t)
#define fw_adc_enable SH03_FN(0x080015d8,void,uint32_t,uint8_t)
#define fw_adc_dma SH03_FN(0x08001604,void,uint32_t,uint8_t)
#endif
