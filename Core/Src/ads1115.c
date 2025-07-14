// ads1115.c
#include "ads1115.h"
#include "i2c.h"
#include "stm32f4xx_hal.h"

#define ADS1115_ADDR     (0x48 << 1)
#define I2C_TIMEOUT      100

int16_t ads_read_channel(uint8_t ch) {
    if (ch > 3) return 0;

    uint8_t config[] = {0x01, 0xC1 | (ch << 4), 0x83};  // MUX config
    uint8_t pointer = 0x00;
    uint8_t result[2] = {0};

    if (HAL_I2C_Master_Transmit(&hi2c1, ADS1115_ADDR, config, 3, I2C_TIMEOUT) != HAL_OK) return 0;
    HAL_Delay(10);
    if (HAL_I2C_Master_Transmit(&hi2c1, ADS1115_ADDR, &pointer, 1, I2C_TIMEOUT) != HAL_OK) return 0;
    if (HAL_I2C_Master_Receive(&hi2c1, ADS1115_ADDR, result, 2, I2C_TIMEOUT) != HAL_OK) return 0;

    return (int16_t)((result[0] << 8) | result[1]);
}
