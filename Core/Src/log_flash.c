#include "main.h"        // GPIOA, GPIO_PIN_4, and HAL defines
#include "spi.h"         // hspi1 (your SPI handle)
#include <string.h>      // for memcpy or memset if used
#include "log_flash.h"
#include "gpio.h"

#define FLASH_TOTAL_SIZE  (64 * 1024)  // 64KB flash
#define FLASH_PAGE_SIZE   256
#define FLASH_ENTRY_SIZE  sizeof(log_entry_t)
#define LOG_ENTRY_SIZE sizeof(log_entry_t)
#define LOG_FLASH_BASE_ADDR 0x0000  // You can change this if needed
#define FLASH_SECTOR_SIZE  4096  // Typical for GD25D05CT and similar flash


static uint32_t log_write_address = 0x0000;

_Static_assert(sizeof(log_entry_t) == 16, "log_entry_t size mismatch!");
uint32_t flash_log_index = 0;
calib_t m1_cal = { .dry = 3000, .wet = 1500 };  // Example defaults
calib_t m2_cal = { .dry = 3000, .wet = 1500 };

static void flash_select(void) {
    HAL_GPIO_WritePin(GPIOA, GPIO_PIN_4, GPIO_PIN_RESET);
}

static void flash_deselect(void) {
    HAL_GPIO_WritePin(GPIOA, GPIO_PIN_4, GPIO_PIN_SET);
}

static void flash_write_enable(void) {
    uint8_t cmd = 0x06;
    flash_select();
    HAL_SPI_Transmit(&hspi1, &cmd, 1, HAL_MAX_DELAY);
    flash_deselect();
}

static void flash_sector_erase(uint32_t addr) {
    uint8_t cmd[4] = {
        0x20,                      // Sector erase command
        (addr >> 16) & 0xFF,
        (addr >> 8)  & 0xFF,
        (addr >> 0)  & 0xFF
    };
    flash_write_enable();
    flash_select();
    HAL_SPI_Transmit(&hspi1, cmd, 4, HAL_MAX_DELAY);
    flash_deselect();
    HAL_Delay(50);  // wait for erase to complete
}


void flash_write_log_entry(const log_entry_t *entry) {
    uint32_t addr = flash_log_index * LOG_ENTRY_SIZE;
    if (addr + LOG_ENTRY_SIZE > FLASH_TOTAL_SIZE) return;

    // 🔧 New: Erase if at beginning of sector
    if ((addr % FLASH_SECTOR_SIZE) == 0) {
        flash_sector_erase(addr);
    }

    flash_write_enable();

    uint8_t cmd[4] = {
        0x02,                      // Page program
        (addr >> 16) & 0xFF,
        (addr >> 8)  & 0xFF,
        (addr >> 0)  & 0xFF
    };

    flash_select();
    HAL_SPI_Transmit(&hspi1, cmd, 4, HAL_MAX_DELAY);
    HAL_SPI_Transmit(&hspi1, (uint8_t*)entry, LOG_ENTRY_SIZE, HAL_MAX_DELAY);
    flash_deselect();

    HAL_Delay(5);  // Ensure flash write completes

    flash_log_index++;
}


void flash_read_log_entry(uint32_t index, log_entry_t *entry) {
    uint32_t addr = index * LOG_ENTRY_SIZE;
    if (addr + LOG_ENTRY_SIZE > FLASH_TOTAL_SIZE) return;

    uint8_t cmd[4] = {
        0x03,                      // Read data
        (addr >> 16) & 0xFF,
        (addr >> 8)  & 0xFF,
        (addr >> 0)  & 0xFF
    };

    flash_select();
    HAL_SPI_Transmit(&hspi1, cmd, 4, HAL_MAX_DELAY);
    HAL_SPI_Receive(&hspi1, (uint8_t*)entry, LOG_ENTRY_SIZE, HAL_MAX_DELAY);
    flash_deselect();
}
