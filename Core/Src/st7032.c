#include "st7032.h"
#include "string.h"
#include "stdio.h"

static I2C_HandleTypeDef *_lcd_i2c;

static void st7032_write(uint8_t control, uint8_t data) {
    uint8_t buf[2] = { control, data };
    HAL_I2C_Master_Transmit(_lcd_i2c, ST7032_ADDR, buf, 2, HAL_MAX_DELAY);
}

void st7032_write_data(uint8_t data) {
    st7032_write(ST7032_DATA, data);
}

static const char bar_chars[] = {
    0xFF, // Full block
    '-',  // Partial or placeholder
};

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

void st7032_draw_moisture_bar(uint8_t line, uint8_t percent) {
    uint8_t full_chars = percent / 10;       // Full blocks
    uint8_t partial_index = (percent % 10) / 2;  // 0-4

    st7032_set_cursor(line, 0);
    st7032_write_str("|");

    for (uint8_t i = 0; i < 10; i++) {
        if (i < full_chars) {
            st7032_write_data(5);  // Full
        } else if (i == full_chars) {
            st7032_write_data(partial_index);
        } else {
            st7032_write_data(0);  // Empty
        }
    }

    st7032_write_str("|");

    char pct[6];
    snprintf(pct, sizeof(pct), " %3d%%", percent);
    st7032_write_str(pct);
}

void st7032_init_bar_chars(void) {
    static const uint8_t bar_chars[6][8] = {
        {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00},  // Empty
        {0x10,0x10,0x10,0x10,0x10,0x10,0x10,0x10},  // 1/5
        {0x18,0x18,0x18,0x18,0x18,0x18,0x18,0x18},  // 2/5
        {0x1C,0x1C,0x1C,0x1C,0x1C,0x1C,0x1C,0x1C},  // 3/5
        {0x1E,0x1E,0x1E,0x1E,0x1E,0x1E,0x1E,0x1E},  // 4/5
        {0x1F,0x1F,0x1F,0x1F,0x1F,0x1F,0x1F,0x1F}   // Full
    };

    for (uint8_t i = 0; i < 6; i++) {
    	st7032_write(ST7032_CMD, 0x40 | (i << 3));  // Set CGRAM address
        for (uint8_t j = 0; j < 8; j++) {
        	st7032_write_data(bar_chars[i][j]);     // Write pixel rows
        }
    }
}



