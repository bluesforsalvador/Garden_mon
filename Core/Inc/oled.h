#ifndef OLED_H
#define OLED_H

#include "u8g2.h"

extern u8g2_t u8g2;

void oled_init(void);
void OledDisplayTask(void *argument);
uint8_t u8x8_byte_hw_i2c_hal_stm32(u8x8_t *, uint8_t, uint8_t, void *);


#endif
