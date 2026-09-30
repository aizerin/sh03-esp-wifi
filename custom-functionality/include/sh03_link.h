#ifndef SH03_LINK_H
#define SH03_LINK_H
#include <stdint.h>
void sh03_uart4_irq(void);
void sh03_link_init(void);
void sh03_link_poll(void);
/* Called only by the interactive task under its UI mutex. */
uint8_t sh03_remote_command(uint8_t chamber, uint8_t operation, uint32_t value);
extern volatile uint8_t sh03_heater_percent[2];
#endif
