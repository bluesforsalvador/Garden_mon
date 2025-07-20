#ifndef U8G2_STM32_HAL_H
#define U8G2_STM32_HAL_H

#include "u8g2.h"

uint8_t u8x8_byte_stm32_hw_i2c(u8x8_t *u8x8, uint8_t msg, uint8_t arg_int, void *arg_ptr);
uint8_t u8x8_gpio_and_delay_stm32(u8x8_t *u8x8, uint8_t msg, uint8_t arg_int, void *arg_ptr);

#endif // U8G2_STM32_HAL_H
