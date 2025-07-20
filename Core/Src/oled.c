#include "u8g2.h"
#include "adc.h"
#include "oled.h"
#include "cmsis_os.h"
#include <stdio.h>
#include "moisture.h"
#include "u8g2.h"
#include "u8x8.h"
#include "i2c.h"
#include "usart.h"
#include <stdarg.h>
#include <string.h>

u8g2_t u8g2;  // Define the actual instance here
extern moisture_cal_t m1_cal, m2_cal;
extern uint8_t u8x8_byte_sw_i2c(u8x8_t *, uint8_t, uint8_t, void *);
extern uint8_t u8x8_gpio_and_delay_stm32(u8x8_t *, uint8_t, uint8_t, void *);

static uint8_t moisture_pct(uint16_t raw, uint16_t dry, uint16_t wet) {
    if (raw >= dry) return 0;
    if (raw <= wet) return 100;
    return 100 * (dry - raw) / (dry - wet);
}

void debug_printf(const char *fmt, ...) {
    char buffer[128];
    va_list args;
    va_start(args, fmt);
    int len = vsnprintf(buffer, sizeof(buffer), fmt, args);
    va_end(args);

    if (len > 0) {
        HAL_UART_Transmit(&huart6, (uint8_t *)buffer, len, HAL_MAX_DELAY);
    }
}

void oled_test_basic_i2c(void) {
    debug_printf("Running OLED I2C test...\n");

    // Display OFF (0xAE)
    uint8_t cmd_off[] = { 0x00, 0xAE };
    if (HAL_I2C_Master_Transmit(&hi2c1, 0x3C << 1, cmd_off, sizeof(cmd_off), HAL_MAX_DELAY) == HAL_OK)
        debug_printf("Display OFF command acknowledged\n");
    else
        debug_printf("NACK on Display OFF command\n");

    HAL_Delay(100);

    // Display ON (0xAF)
    uint8_t cmd_on[] = { 0x00, 0xAF };
    if (HAL_I2C_Master_Transmit(&hi2c1, 0x3C << 1, cmd_on, sizeof(cmd_on), HAL_MAX_DELAY) == HAL_OK)
        debug_printf("Display ON command acknowledged\n");
    else
        debug_printf("NACK on Display ON command\n");
}

void oled_test_draw_line(void) {
    debug_printf("Drawing test line...\n");

    // Set page to 0
    uint8_t set_page[] = { 0x00, 0xB0 };
    HAL_I2C_Master_Transmit(&hi2c1, 0x3C << 1, set_page, sizeof(set_page), HAL_MAX_DELAY);

    // Set column address to 0
    uint8_t set_col[] = { 0x00, 0x00, 0x10 };
    HAL_I2C_Master_Transmit(&hi2c1, 0x3C << 1, set_col, sizeof(set_col), HAL_MAX_DELAY);

    // Fill data: 0x40 is control byte for display RAM
    uint8_t data[129];
    data[0] = 0x40;  // Control byte for data
    for (int i = 1; i < 129; i++) {
        data[i] = 0xFF;  // Full white line
    }

    HAL_I2C_Master_Transmit(&hi2c1, 0x3C << 1, data, sizeof(data), HAL_MAX_DELAY);
    debug_printf("Test line sent to display\r\n");
}


// ADD THIS FUNCTION TO FIX "undefined reference to `oled_init`"
void oled_init(void) {
    debug_printf("OLED init start\r\n");
    // Set up SSD1306 128x64 I2C display
	u8g2_Setup_ssd1306_i2c_128x64_noname_f(
	    &u8g2,U8G2_R0,
	    u8x8_byte_hw_i2c_hal_stm32,
	    u8x8_gpio_and_delay_stm32
	);
	u8x8_SetI2CAddress(&u8g2.u8x8, 0x3C << 1);  // Set OLED to 0x78 (8-bit addr)
	debug_printf("I2C Address Set to 0x%02X\r\n", u8g2.u8x8.i2c_address);

	debug_printf("u8g2_Setup done\r\n");

    u8g2_InitDisplay(&u8g2);
    debug_printf("u8g2_InitDisplay done\r\n");
    u8g2_SetPowerSave(&u8g2, 0);  // Wake up display
    debug_printf("Display power save off\r\n");
    u8g2_SetFont(&u8g2, u8g2_font_ncenB08_tr);  // Optional default font
    u8g2_SendF(&u8g2, "c", 0x81);  // Set Contrast
    u8g2_SendF(&u8g2, "c", 0xFF);  // Maximum brightness
    debug_printf("Font set\r\n");
    u8g2_ClearBuffer(&u8g2);
    u8g2_DrawStr(&u8g2, 0, 24, "Hello OLED\r\n");
    u8g2_SendBuffer(&u8g2);
    debug_printf("Hello OLED drawn\r\n");
}

void OledDisplayTask(void *argument) {
    ADC_ChannelConfTypeDef sConfig = {0};
    uint32_t val_m1 = 0, val_m2 = 0;
    char line[32];

    for (;;) {

        // --- Read M1 ---
        sConfig.Channel = ADC_CHANNEL_0;
        sConfig.Rank = 1;
        sConfig.SamplingTime = ADC_SAMPLETIME_3CYCLES;
        HAL_ADC_ConfigChannel(&hadc1, &sConfig);
        HAL_ADC_Start(&hadc1);
        HAL_ADC_PollForConversion(&hadc1, HAL_MAX_DELAY);
        val_m1 = HAL_ADC_GetValue(&hadc1);

        // --- Read M2 ---
        sConfig.Channel = ADC_CHANNEL_1;
        HAL_ADC_ConfigChannel(&hadc1, &sConfig);
        HAL_ADC_Start(&hadc1);
        HAL_ADC_PollForConversion(&hadc1, HAL_MAX_DELAY);
        val_m2 = HAL_ADC_GetValue(&hadc1);

        // --- Convert to % ---
        uint8_t m1_pct = moisture_pct(val_m1, m1_cal.dry, m1_cal.wet);
        uint8_t m2_pct = moisture_pct(val_m2, m2_cal.dry, m2_cal.wet);

        // --- Draw UI ---
        u8g2_ClearBuffer(&u8g2);

        // M1
        snprintf(line, sizeof(line), "M1: %3d%%", m1_pct);
        u8g2_DrawStr(&u8g2, 0, 15, line);
        u8g2_DrawFrame(&u8g2, 40, 7, 60, 8);  // Bar
        u8g2_DrawBox(&u8g2, 40, 7, (60 * m1_pct) / 100, 8);  // Fill
        u8g2_DrawVLine(&u8g2, 40, 4, 12);  // left tick = dry
        u8g2_DrawVLine(&u8g2, 99, 4, 12);  // right tick = wet

        // M2
        snprintf(line, sizeof(line), "M2: %3d%%", m2_pct);
        u8g2_DrawStr(&u8g2, 0, 35, line);
        u8g2_DrawFrame(&u8g2, 40, 27, 60, 8);
        u8g2_DrawBox(&u8g2, 40, 27, (60 * m2_pct) / 100, 8);
        u8g2_DrawVLine(&u8g2, 40, 4, 12);  // left tick = dry
        u8g2_DrawVLine(&u8g2, 99, 4, 12);  // right tick = wet

        u8g2_SendBuffer(&u8g2);

        osDelay(2000);
    }
}
