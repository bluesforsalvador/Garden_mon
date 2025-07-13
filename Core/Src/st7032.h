#ifndef __ST7032_H__
#define __ST7032_H__

#include "main.h"

#define ST7032_ADDR       (0x3E << 1)  // 7-bit address shifted for HAL
#define ST7032_CMD        0x00
#define ST7032_DATA       0x40

void st7032_init(I2C_HandleTypeDef *hi2c);
void st7032_set_cursor(uint8_t row, uint8_t col);
void st7032_write_char(char c);
void st7032_write_str(const char *str);
void st7032_clear(void);

#endif
