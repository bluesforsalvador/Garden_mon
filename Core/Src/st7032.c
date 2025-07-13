#include "st7032.h"
#include "string.h"
#include "stdio.h"

static I2C_HandleTypeDef *_lcd_i2c;

static void st7032_write(uint8_t control, uint8_t data) {
    uint8_t buf[2] = { control, data };
    HAL_I2C_Master_Transmit(_lcd_i2c, ST7032_ADDR, buf, 2, HAL_MAX_DELAY);
}

void st7032_init(I2C_HandleTypeDef *hi2c) {
    _lcd_i2c = hi2c;

    HAL_Delay(50); // Wait for power on

    st7032_write(ST7032_CMD, 0x38); // Function set
    st7032_write(ST7032_CMD, 0x39); // Function set extended
    st7032_write(ST7032_CMD, 0x14); // Internal OSC
    st7032_write(ST7032_CMD, 0x70); // Contrast set low nibble
    st7032_write(ST7032_CMD, 0x56); // Power/Icon/Contrast high nibble
    st7032_write(ST7032_CMD, 0x6C); // Follower control
    HAL_Delay(200);
    st7032_write(ST7032_CMD, 0x38); // Function set normal
    st7032_write(ST7032_CMD, 0x0C); // Display ON
    st7032_write(ST7032_CMD, 0x01); // Clear display
    HAL_Delay(2);
}

void st7032_set_cursor(uint8_t row, uint8_t col) {
    uint8_t addr = (row == 0) ? 0x00 : 0x40;
    addr += col;
    st7032_write(ST7032_CMD, 0x80 | addr);
}

void st7032_write_char(char c) {
    st7032_write(ST7032_DATA, (uint8_t)c);
}

void st7032_write_str(const char *str) {
    while (*str) {
        st7032_write_char(*str++);
    }
}

void st7032_clear(void) {
    st7032_write(ST7032_CMD, 0x01);
    HAL_Delay(2);
}
