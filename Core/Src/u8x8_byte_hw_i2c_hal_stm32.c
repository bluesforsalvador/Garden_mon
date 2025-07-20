#include "u8g2.h"
#include "i2c.h"

uint8_t u8x8_byte_hw_i2c_hal_stm32(u8x8_t *u8x8, uint8_t msg, uint8_t arg_int, void *arg_ptr) {
    static uint8_t buffer[32];
    static uint8_t buf_idx;

    switch (msg) {
        case U8X8_MSG_BYTE_INIT:
            // Already initialized via CubeMX
            return 1;

        case U8X8_MSG_BYTE_START_TRANSFER:
            buf_idx = 0;
            return 1;

        case U8X8_MSG_BYTE_SEND:
            if ((buf_idx + arg_int) >= sizeof(buffer)) return 0;
            memcpy(&buffer[buf_idx], (uint8_t *)arg_ptr, arg_int);
            buf_idx += arg_int;
            return 1;

        case U8X8_MSG_BYTE_END_TRANSFER:
            if (HAL_I2C_Master_Transmit(&hi2c1, u8x8_GetI2CAddress(u8x8), buffer, buf_idx, HAL_MAX_DELAY) != HAL_OK)
                return 0;
            return 1;

        case U8X8_MSG_BYTE_SET_DC:
            // Not used for I2C
            return 1;

        default:
            return 0;
    }
}
