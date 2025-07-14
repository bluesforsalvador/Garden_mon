#include "cli.h"
#include "usart.h"
#include "gpio.h"
#include "adc.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include "FreeRTOS.h"
#include "task.h"
#include "i2c.h"
#include "cmsis_os.h"
#include "st7032.h"
#include "spi.h"
#include "log_flash.h"
#include "ads1115.h"


#define CLI_BUFFER_SIZE 64
#define MAX_COMMANDS 20
#define MAX_ARGS 10
#define I2C_TIMEOUT 100
#define CLI_INPUT_QUEUE_LEN 1
#define CLI_DMA_RX_BUFFER_SIZE 128
#define CLI_HISTORY_SIZE 10  // number of commands to keep
#define FLASH_TOTAL_SIZE    (64 * 1024)  // 64KB
#define FLASH_PAGE_SIZE     256
#define FLASH_SECTOR_SIZE   4096

static uint8_t dma_rx_buf[CLI_DMA_RX_BUFFER_SIZE];
static uint16_t dma_rx_pos = 0;
static char cli_history[CLI_HISTORY_SIZE][CLI_BUFFER_SIZE];
static int history_count = 0;       // how many commands stored
static int history_index = 0;       // where to store next
static int history_browse = -1;     // -1 = not browsing

extern UART_HandleTypeDef huart6;
extern ADC_HandleTypeDef hadc1;
extern I2C_HandleTypeDef hi2c1;
extern SPI_HandleTypeDef hspi1;

static const cli_command_t *command_table[MAX_COMMANDS];
static size_t command_count = 0;

static uint8_t rx_byte = 0;
static osMessageQueueId_t cli_input_queue;
static char temp_line[CLI_BUFFER_SIZE];
static uint8_t temp_index = 0;

static char last_command[CLI_BUFFER_SIZE] = {0};
static uint8_t escape_state = 0; // Tracks ESC sequence

static int16_t ads_results[4] = {0};

typedef struct {
    char line[CLI_BUFFER_SIZE];
} cli_input_t;

// --- Forward Declarations ---
static void CLI_ProcessCommand(const char *cmd);
static void cmd_help(int argc, char **argv);
static void cmd_led(int argc, char **argv);
static void cmd_adc(int argc, char **argv);
static void cmd_i2c(int argc, char **argv);
static void cmd_ads(int argc, char **argv);
static void cmd_lcd(int argc, char **argv);
static void cmd_flash(int argc, char **argv);
static void cmd_flash_test(int argc, char **argv);
static void cmd_flash_test_full(int argc, char **argv);
static void cmd_logtest(int argc, char **argv);
static void cmd_logindex(int argc, char **argv);
static void cmd_logdump(int argc, char **argv);
static void cmd_moistcal(int argc, char **argv);

// --- Command Table ---
static const cli_command_t commands[] = {
    { "help",  "help         - Show command list",             cmd_help },
    { "led",   "led on/off   - Control LED",                  cmd_led },
    { "read",  "read M1/M2    - Read moisture sensors",        cmd_adc },
    { "i2c",   "i2c scan | read | write",                     cmd_i2c },
    { "i2cr",  "alias: i2c read",                             cmd_i2c },
    { "i2cw",  "alias: i2c write",                            cmd_i2c },
    { "ads",   "ads           - Read all ADS1115 inputs",      cmd_ads },
    { "lcd",   "lcd write <line> <text>",                     cmd_lcd },
    { "flash", "flash id      - Read JEDEC ID from SPI flash", cmd_flash },
    { "ftest", "ftest         - Stress test flash R/W", cmd_flash_test },
	{ "ftestfull", "ftestfull     - Full flash write/read/verify", cmd_flash_test_full },
	{ "logtest", "logtest       - Write test entry to flash", cmd_logtest },
	{ "logindex", "logindex      - Show current flash log index", cmd_logindex },
	{ "logdump", "logdump N|all - Dump last N or all log entries", cmd_logdump },
	{ "moistcal", "moistcal 1|2|both - Calibrate moisture sensor(s)", cmd_moistcal },


};

void CLI_RegisterCommands(const cli_command_t *table, size_t count) {
    if (count > MAX_COMMANDS) count = MAX_COMMANDS;
    for (size_t i = 0; i < count; i++) {
        command_table[i] = &table[i];
    }
    command_count = count;
}

void CLI_Task(void *argument) {
    cli_input_queue = osMessageQueueNew(CLI_INPUT_QUEUE_LEN, sizeof(cli_input_t), NULL);
    CLI_RegisterCommands(commands, sizeof(commands) / sizeof(commands[0]));
    HAL_UART_Transmit(&huart6, (uint8_t*)"CLI Task running\r\n> ", 22, HAL_MAX_DELAY);

    // Debug print before DMA setup
	HAL_UART_Transmit(&huart6, (uint8_t*)"Initializing CLI DMA...\r\n", 26, HAL_MAX_DELAY);

	if (HAL_UART_Receive_DMA(&huart6, dma_rx_buf, CLI_DMA_RX_BUFFER_SIZE) != HAL_OK) {
	   HAL_UART_Transmit(&huart6, (uint8_t*)"ERROR: UART DMA start failed\r\n", 31, HAL_MAX_DELAY);
	} else {
	   HAL_UART_Transmit(&huart6, (uint8_t*)"CLI DMA active\r\n> ", 19, HAL_MAX_DELAY);
	}

    TickType_t lastUpdate = xTaskGetTickCount();

    while (1) {
        uint16_t pos = CLI_DMA_RX_BUFFER_SIZE - __HAL_DMA_GET_COUNTER(huart6.hdmarx);
        while (dma_rx_pos != pos) {
            uint8_t ch = dma_rx_buf[dma_rx_pos++];
            if (dma_rx_pos >= CLI_DMA_RX_BUFFER_SIZE)
                dma_rx_pos = 0;
            if (escape_state == 0 && ch == 0x1B) {
                escape_state = 1;  // ESC received
                continue;
            } else if (escape_state == 1 && ch == '[') {
                escape_state = 2;  // '[' received
                continue;
            } else if (escape_state == 2 && ch == 'A') {
                // Up arrow detected
                escape_state = 0;

                if (history_count == 0) continue;

                if (history_browse < history_count - 1)
                    history_browse++;

                int idx = (CLI_HISTORY_SIZE + history_index - history_browse - 1) % CLI_HISTORY_SIZE;

                // Clear current input line visually
                while (temp_index--) HAL_UART_Transmit(&huart6, (uint8_t*)"\b \b", 3, HAL_MAX_DELAY);
                temp_index = 0;

                // Copy history entry into temp_line
                strncpy(temp_line, cli_history[idx], CLI_BUFFER_SIZE);
                temp_index = strlen(temp_line);

                HAL_UART_Transmit(&huart6, (uint8_t*)temp_line, temp_index, HAL_MAX_DELAY);
                continue;
            } else if (escape_state != 0) {
                escape_state = 0;  // Reset unknown escape
                continue;
            }
            if (ch == '\r' || ch == '\n') {
                temp_line[temp_index] = '\0';
                CLI_ProcessCommand(temp_line);
                temp_index = 0;
                HAL_UART_Transmit(&huart6, (uint8_t*)"\r\n> ", 4, HAL_MAX_DELAY);
            } else if (ch == 0x7F || ch == '\b') {
                if (temp_index > 0) {
                    temp_index--;
                    HAL_UART_Transmit(&huart6, (uint8_t*)"\b \b", 3, HAL_MAX_DELAY);
                }
            } else if (temp_index < CLI_BUFFER_SIZE - 1) {
                temp_line[temp_index++] = ch;
                HAL_UART_Transmit(&huart6, &ch, 1, HAL_MAX_DELAY); // echo
            }
        }

        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

static void CLI_ProcessCommand(const char *cmd) {
	if (cmd[0] != '\0') {
	    strncpy(cli_history[history_index], cmd, CLI_BUFFER_SIZE);
	    history_index = (history_index + 1) % CLI_HISTORY_SIZE;
	    if (history_count < CLI_HISTORY_SIZE) history_count++;
	    history_browse = -1; // reset browsing on new input
	}
    char *argv[MAX_ARGS] = {0};
    char buffer[CLI_BUFFER_SIZE];
    strncpy(buffer, cmd, CLI_BUFFER_SIZE);

    int argc = 0;
    char *token = strtok(buffer, " ");
    while (token && argc < MAX_ARGS) {
        argv[argc++] = token;
        token = strtok(NULL, " ");
    }

    if (argc == 0) return;

    for (size_t i = 0; i < command_count; i++) {
        if (strcmp(argv[0], command_table[i]->name) == 0) {
        	strncpy(last_command, cmd, CLI_BUFFER_SIZE);
        	command_table[i]->handler(argc, argv);
            return;
        }
    }

    HAL_UART_Transmit(&huart6, (uint8_t*)"Unknown command\r\n", 18, HAL_MAX_DELAY);
}

static void cmd_help(int argc, char **argv) {
    for (size_t i = 0; i < command_count; i++) {
        const char *line = command_table[i]->help;
        HAL_UART_Transmit(&huart6, (uint8_t*)line, strlen(line), HAL_MAX_DELAY);
        HAL_UART_Transmit(&huart6, (uint8_t*)"\r\n", 2, HAL_MAX_DELAY);
    }
}

static void cmd_led(int argc, char **argv) {
    if (argc < 2) {
        HAL_UART_Transmit(&huart6, (uint8_t*)"Usage: led on/off\r\n", 21, HAL_MAX_DELAY);
        return;
    }
    if (strcmp(argv[1], "on") == 0) {
        HAL_GPIO_WritePin(GPIOB, GPIO_PIN_12, GPIO_PIN_SET);
        HAL_UART_Transmit(&huart6, (uint8_t*)"LED ON\r\n", 8, HAL_MAX_DELAY);
    } else if (strcmp(argv[1], "off") == 0) {
        HAL_GPIO_WritePin(GPIOB, GPIO_PIN_12, GPIO_PIN_RESET);
        HAL_UART_Transmit(&huart6, (uint8_t*)"LED OFF\r\n", 9, HAL_MAX_DELAY);
    }
}

static void wait_for_enter(void) {
    // Wait for a single '\r' to appear in the DMA buffer
    uint16_t last_pos = CLI_DMA_RX_BUFFER_SIZE - __HAL_DMA_GET_COUNTER(huart6.hdmarx);
    while (1) {
        uint16_t new_pos = CLI_DMA_RX_BUFFER_SIZE - __HAL_DMA_GET_COUNTER(huart6.hdmarx);
        while (last_pos != new_pos) {
            uint8_t ch = dma_rx_buf[last_pos++];
            if (last_pos >= CLI_DMA_RX_BUFFER_SIZE) last_pos = 0;
            if (ch == '\r' || ch == '\n') return;  // ENTER detected
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

static void cmd_adc(int argc, char **argv) {
    if (argc < 2) {
        HAL_UART_Transmit(&huart6, (uint8_t*)"Usage: read M1/M2\r\n", 20, HAL_MAX_DELAY);
        return;
    }

    uint32_t val = 0;
    ADC_ChannelConfTypeDef sConfig = {0};
    sConfig.Rank = 1;
    sConfig.SamplingTime = ADC_SAMPLETIME_3CYCLES;

    if (strcmp(argv[1], "M1") == 0) {
        sConfig.Channel = ADC_CHANNEL_0;
    } else if (strcmp(argv[1], "M2") == 0) {
        sConfig.Channel = ADC_CHANNEL_1;
    } else {
        HAL_UART_Transmit(&huart6, (uint8_t*)"Invalid channel. Use M1 or M2\r\n", 31, HAL_MAX_DELAY);
        return;
    }

    HAL_ADC_ConfigChannel(&hadc1, &sConfig);
    HAL_ADC_Start(&hadc1);
    HAL_ADC_PollForConversion(&hadc1, HAL_MAX_DELAY);
    val = HAL_ADC_GetValue(&hadc1);

    char msg[64];
    snprintf(msg, sizeof(msg), "MCU ADC %s: %lu\r\n", argv[1], val);
    HAL_UART_Transmit(&huart6, (uint8_t*)msg, strlen(msg), HAL_MAX_DELAY);
}

static void cmd_ads(int argc, char **argv) {
    uint8_t config[] = {0x01, 0xC1, 0x83};
    uint8_t pointer = 0x00;
    uint8_t result[2];
    char msg[64];

    for (uint8_t ch = 0; ch < 4; ch++) {
        config[1] = 0xC1 | (ch << 4);  // Change mux
        if (HAL_I2C_Master_Transmit(&hi2c1, 0x48 << 1, config, 3, I2C_TIMEOUT) != HAL_OK) continue;
        HAL_Delay(10);
        if (HAL_I2C_Master_Transmit(&hi2c1, 0x48 << 1, &pointer, 1, I2C_TIMEOUT) != HAL_OK) continue;
        if (HAL_I2C_Master_Receive(&hi2c1, 0x48 << 1, result, 2, I2C_TIMEOUT) != HAL_OK) continue;
        int16_t value = (result[0] << 8) | result[1];
        snprintf(msg, sizeof(msg), "ADS CH%d: %d\r\n", ch, value);
        HAL_UART_Transmit(&huart6, (uint8_t*)msg, strlen(msg), HAL_MAX_DELAY);
    }
}

static void cmd_i2c(int argc, char **argv) {
    if (argc >= 2 && strcmp(argv[1], "scan") == 0) {
        HAL_UART_Transmit(&huart6, (uint8_t*)"     00 01 02 03 04 05 06 07 08 09 0A 0B 0C 0D 0E 0F\r\n", 53, HAL_MAX_DELAY);
        for (int row = 0; row < 8; row++) {
            char line[128];
            snprintf(line, sizeof(line), "0x%02X:", row << 4);
            HAL_UART_Transmit(&huart6, (uint8_t*)line, strlen(line), HAL_MAX_DELAY);
            for (int col = 0; col < 16; col++) {
                uint8_t addr = (row << 4) | col;
                if (addr < 0x03 || addr > 0x77) {
                    HAL_UART_Transmit(&huart6, (uint8_t*)"   ", 3, HAL_MAX_DELAY);
                    continue;
                }
                if (HAL_I2C_IsDeviceReady(&hi2c1, addr << 1, 3, 10) == HAL_OK) {
                    snprintf(line, sizeof(line), " %02X", addr);
                } else {
                    snprintf(line, sizeof(line), " --");
                }
                HAL_UART_Transmit(&huart6, (uint8_t*)line, 3, HAL_MAX_DELAY);
            }
            HAL_UART_Transmit(&huart6, (uint8_t*)"\r\n", 2, HAL_MAX_DELAY);
        }
        return;
    }
    HAL_UART_Transmit(&huart6, (uint8_t*)"Usage: i2c scan\r\n", 18, HAL_MAX_DELAY);
}

static void cmd_lcd(int argc, char **argv) {
    if (argc < 4 || strcmp(argv[1], "write") != 0) {
        HAL_UART_Transmit(&huart6, (uint8_t*)"Usage: lcd write <line> <text>\r\n", 33, HAL_MAX_DELAY);
        return;
    }
    uint8_t line = atoi(argv[2]);
    if (line > 1) line = 0;
    st7032_set_cursor(line, 0);
    st7032_write_str(argv[3]);
    HAL_UART_Transmit(&huart6, (uint8_t*)"LCD write OK\r\n", 14, HAL_MAX_DELAY);
}

static void cmd_flash(int argc, char **argv) {
    if (argc < 2 || strcmp(argv[1], "id") != 0) {
        HAL_UART_Transmit(&huart6, (uint8_t*)"Usage: flash id\r\n", 17, HAL_MAX_DELAY);
        return;
    }
    uint8_t cmd = 0x9F;
    uint8_t id[3] = {0};

    HAL_GPIO_WritePin(GPIOA, GPIO_PIN_4, GPIO_PIN_RESET);
    if (HAL_SPI_Transmit(&hspi1, &cmd, 1, 100) != HAL_OK) goto fail;
    if (HAL_SPI_Receive(&hspi1, id, 3, 100) != HAL_OK) goto fail;
    HAL_GPIO_WritePin(GPIOA, GPIO_PIN_4, GPIO_PIN_SET);

    char msg[64];
    snprintf(msg, sizeof(msg), "Flash ID: %02X %02X %02X\r\n", id[0], id[1], id[2]);
    HAL_UART_Transmit(&huart6, (uint8_t*)msg, strlen(msg), HAL_MAX_DELAY);
    return;

fail:
    HAL_GPIO_WritePin(GPIOA, GPIO_PIN_4, GPIO_PIN_SET);
    HAL_UART_Transmit(&huart6, (uint8_t*)"Flash read failed\r\n", 20, HAL_MAX_DELAY);
}

static void flash_write_enable(void) {
    uint8_t cmd = 0x06;
    HAL_GPIO_WritePin(GPIOA, GPIO_PIN_4, GPIO_PIN_RESET);
    HAL_SPI_Transmit(&hspi1, &cmd, 1, 100);
    HAL_GPIO_WritePin(GPIOA, GPIO_PIN_4, GPIO_PIN_SET);
}

static void flash_sector_erase(uint32_t addr) {
    uint8_t cmd[4] = { 0x20, (addr >> 16), (addr >> 8), addr };
    flash_write_enable();
    HAL_GPIO_WritePin(GPIOA, GPIO_PIN_4, GPIO_PIN_RESET);
    HAL_SPI_Transmit(&hspi1, cmd, 4, 100);
    HAL_GPIO_WritePin(GPIOA, GPIO_PIN_4, GPIO_PIN_SET);
    HAL_Delay(50);  // Sector erase time
}

static void flash_write_page(uint32_t addr, uint8_t *data, uint32_t len) {
    uint8_t cmd[4] = { 0x02, (addr >> 16), (addr >> 8), addr };
    flash_write_enable();
    HAL_GPIO_WritePin(GPIOA, GPIO_PIN_4, GPIO_PIN_RESET);
    HAL_SPI_Transmit(&hspi1, cmd, 4, 100);
    HAL_SPI_Transmit(&hspi1, data, len, 500);
    HAL_GPIO_WritePin(GPIOA, GPIO_PIN_4, GPIO_PIN_SET);
    HAL_Delay(5);
}

static void flash_read_page(uint32_t addr, uint8_t *data, uint32_t len) {
    uint8_t cmd[4] = { 0x03, (addr >> 16), (addr >> 8), addr };
    HAL_GPIO_WritePin(GPIOA, GPIO_PIN_4, GPIO_PIN_RESET);
    HAL_SPI_Transmit(&hspi1, cmd, 4, 100);
    HAL_SPI_Receive(&hspi1, data, len, 500);
    HAL_GPIO_WritePin(GPIOA, GPIO_PIN_4, GPIO_PIN_SET);
}


static void cmd_flash_test(int argc, char **argv) {
    const uint32_t size = 256;
    uint8_t tx[size], rx[size];

    for (uint32_t i = 0; i < size; i++) tx[i] = i;

    uint32_t startTick, endTick;
    char msg[128];

    // --- Write Enable ---
    uint8_t we = 0x06;
    HAL_GPIO_WritePin(GPIOA, GPIO_PIN_4, GPIO_PIN_RESET);
    HAL_SPI_Transmit(&hspi1, &we, 1, 100);
    HAL_GPIO_WritePin(GPIOA, GPIO_PIN_4, GPIO_PIN_SET);

    // --- Write Data ---
    startTick = HAL_GetTick();
    uint8_t cmd[4] = { 0x02, 0x00, 0x00, 0x00 };
    HAL_GPIO_WritePin(GPIOA, GPIO_PIN_4, GPIO_PIN_RESET);
    HAL_SPI_Transmit(&hspi1, cmd, 4, 100);
    HAL_SPI_Transmit(&hspi1, tx, size, 500);
    HAL_GPIO_WritePin(GPIOA, GPIO_PIN_4, GPIO_PIN_SET);
    endTick = HAL_GetTick();
    snprintf(msg, sizeof(msg), "Wrote %lu bytes in %lu ms\r\n", (uint32_t)size, (endTick - startTick));
    HAL_UART_Transmit(&huart6, (uint8_t*)msg, strlen(msg), HAL_MAX_DELAY);

    HAL_Delay(10);  // wait for flash write

    // --- Read Back ---
    startTick = HAL_GetTick();
    uint8_t read_cmd[4] = { 0x03, 0x00, 0x00, 0x00 };
    HAL_GPIO_WritePin(GPIOA, GPIO_PIN_4, GPIO_PIN_RESET);
    HAL_SPI_Transmit(&hspi1, read_cmd, 4, 100);
    HAL_SPI_Receive(&hspi1, rx, size, 500);
    HAL_GPIO_WritePin(GPIOA, GPIO_PIN_4, GPIO_PIN_SET);
    endTick = HAL_GetTick();
    snprintf(msg, sizeof(msg), "Read %lu bytes in %lu ms\r\n", (uint32_t)size, (endTick - startTick));
    HAL_UART_Transmit(&huart6, (uint8_t*)msg, strlen(msg), HAL_MAX_DELAY);

    // --- Verify ---
    int errors = 0;
    for (uint32_t i = 0; i < size; i++) {
        if (rx[i] != tx[i]) errors++;
    }

    snprintf(msg, sizeof(msg), "Flash test %s (%d mismatches)\r\n",
             errors == 0 ? "OK" : "FAIL", errors);
    HAL_UART_Transmit(&huart6, (uint8_t*)msg, strlen(msg), HAL_MAX_DELAY);

    // --- Optional: Dump first few bytes ---
    snprintf(msg, sizeof(msg), "TX: ");
    HAL_UART_Transmit(&huart6, (uint8_t*)msg, strlen(msg), HAL_MAX_DELAY);
    for (int i = 0; i < 8; i++) {
        snprintf(msg, sizeof(msg), "%02X ", tx[i]);
        HAL_UART_Transmit(&huart6, (uint8_t*)msg, strlen(msg), HAL_MAX_DELAY);
    }
    HAL_UART_Transmit(&huart6, (uint8_t*)"\r\nRX: ", 6, HAL_MAX_DELAY);
    for (int i = 0; i < 8; i++) {
        snprintf(msg, sizeof(msg), "%02X ", rx[i]);
        HAL_UART_Transmit(&huart6, (uint8_t*)msg, strlen(msg), HAL_MAX_DELAY);
    }
    HAL_UART_Transmit(&huart6, (uint8_t*)"\r\n", 2, HAL_MAX_DELAY);
}

static void cmd_flash_test_full(int argc, char **argv) {
    uint8_t tx[FLASH_PAGE_SIZE], rx[FLASH_PAGE_SIZE];
    uint32_t errors = 0;
    char msg[128];

    // Fill tx with pattern
    for (uint32_t i = 0; i < FLASH_PAGE_SIZE; i++) {
        tx[i] = i & 0xFF;
    }

    uint32_t start_tick = HAL_GetTick();

    for (uint32_t addr = 0; addr < FLASH_TOTAL_SIZE; addr += FLASH_PAGE_SIZE) {
        if (addr % FLASH_SECTOR_SIZE == 0) {
            snprintf(msg, sizeof(msg), "Erasing sector at 0x%04lX\r\n", addr);
            HAL_UART_Transmit(&huart6, (uint8_t*)msg, strlen(msg), HAL_MAX_DELAY);
            flash_sector_erase(addr);
        }

        flash_write_page(addr, tx, FLASH_PAGE_SIZE);
        flash_read_page(addr, rx, FLASH_PAGE_SIZE);

        for (uint32_t i = 0; i < FLASH_PAGE_SIZE; i++) {
            if (rx[i] != tx[i]) {
                errors++;
                if (errors < 5) {
                    snprintf(msg, sizeof(msg),
                             "Error @ 0x%04lX: wrote 0x%02X, read 0x%02X\r\n",
                             addr + i, tx[i], rx[i]);
                    HAL_UART_Transmit(&huart6, (uint8_t*)msg, strlen(msg), HAL_MAX_DELAY);
                }
            }
        }
    }

    uint32_t elapsed = HAL_GetTick() - start_tick;

    snprintf(msg, sizeof(msg),
             "Done: %lu bytes tested in %lu ms. Errors: %lu\r\n",
             FLASH_TOTAL_SIZE, elapsed, errors);
    HAL_UART_Transmit(&huart6, (uint8_t*)msg, strlen(msg), HAL_MAX_DELAY);
}

static void cmd_logtest(int argc, char **argv) {
    log_entry_t entry;
    ADC_ChannelConfTypeDef sConfig = {0};
    sConfig.Rank = 1;
    sConfig.SamplingTime = ADC_SAMPLETIME_3CYCLES;

    // --- Read M1 (PA0)
    sConfig.Channel = ADC_CHANNEL_0;
    HAL_ADC_ConfigChannel(&hadc1, &sConfig);
    HAL_ADC_Start(&hadc1);
    HAL_ADC_PollForConversion(&hadc1, HAL_MAX_DELAY);
    entry.m1 = HAL_ADC_GetValue(&hadc1);

    // --- Read M2 (PA1)
    sConfig.Channel = ADC_CHANNEL_1;
    HAL_ADC_ConfigChannel(&hadc1, &sConfig);
    HAL_ADC_Start(&hadc1);
    HAL_ADC_PollForConversion(&hadc1, HAL_MAX_DELAY);
    entry.m2 = HAL_ADC_GetValue(&hadc1);

    // --- Read ADS1115 channels
    uint8_t config[] = {0x01, 0xC1, 0x83}; // Start with AIN0
    uint8_t pointer = 0x00;
    uint8_t result[2];
    for (uint8_t ch = 0; ch < 4; ch++) {
        config[1] = 0xC1 | (ch << 4);  // MUX channel select
        HAL_I2C_Master_Transmit(&hi2c1, 0x48 << 1, config, 3, 100);
        HAL_Delay(10);
        HAL_I2C_Master_Transmit(&hi2c1, 0x48 << 1, &pointer, 1, 100);
        HAL_I2C_Master_Receive(&hi2c1, 0x48 << 1, result, 2, 100);
        entry.ads[ch] = (result[0] << 8) | result[1];
    }

    // --- Timestamp
    entry.timestamp_ms = xTaskGetTickCount();

    // --- Flash write
    flash_write_log_entry(&entry);
    HAL_UART_Transmit(&huart6, (uint8_t*)"Entry written\r\n", 15, HAL_MAX_DELAY);

    // --- Readback
    log_entry_t check;
    flash_read_log_entry(flash_log_index - 1, &check);

    char msg[128];
    snprintf(msg, sizeof(msg),
        "Readback:\r\n  Time: %lu\r\n  M1: %u  M2: %u\r\n  ADS: %d %d %d %d\r\n",
        check.timestamp_ms,
        check.m1,
        check.m2,
        check.ads[0], check.ads[1], check.ads[2], check.ads[3]);
    HAL_UART_Transmit(&huart6, (uint8_t*)msg, strlen(msg), HAL_MAX_DELAY);

    // --- Verify
    if (memcmp(&entry, &check, sizeof(log_entry_t)) == 0) {
        HAL_UART_Transmit(&huart6, (uint8_t*)"Log entry verified OK\r\n", 24, HAL_MAX_DELAY);
    } else {
        HAL_UART_Transmit(&huart6, (uint8_t*)"WARNING: mismatch!\r\n", 21, HAL_MAX_DELAY);
    }
}

static void cmd_logdump(int argc, char **argv) {
    uint32_t count = 0;
    char msg[128];

    if (argc < 2) {
        HAL_UART_Transmit(&huart6, (uint8_t*)"Usage: logdump <N|all>\r\n", 25, HAL_MAX_DELAY);
        return;
    }

    if (strcmp(argv[1], "all") == 0) {
        count = flash_log_index;
    } else {
        count = atoi(argv[1]);
        if (count == 0 || count > flash_log_index) count = flash_log_index;
    }

    if (count == 0) {
        HAL_UART_Transmit(&huart6, (uint8_t*)"No log entries to show\r\n", 25, HAL_MAX_DELAY);
        return;
    }

    uint32_t start = flash_log_index - count;

    for (uint32_t i = 0; i < count; i++) {
        log_entry_t entry;
        flash_read_log_entry(start + i, &entry);

        snprintf(msg, sizeof(msg),
            "#%03lu  Time: %lu ms  M1: %u  M2: %u  ADS: %d %d %d %d\r\n",
            start + i,
            entry.timestamp_ms,
            entry.m1, entry.m2,
            entry.ads[0], entry.ads[1], entry.ads[2], entry.ads[3]);

        HAL_UART_Transmit(&huart6, (uint8_t*)msg, strlen(msg), HAL_MAX_DELAY);
    }
}

static void cmd_logindex(int argc, char **argv) {
    char msg[64];
    snprintf(msg, sizeof(msg), "Log index = %lu\r\n", flash_log_index);
    HAL_UART_Transmit(&huart6, (uint8_t*)msg, strlen(msg), HAL_MAX_DELAY);
}

static void cmd_moistcal(int argc, char **argv) {
    char msg[128];

    if (argc < 2) {
        HAL_UART_Transmit(&huart6, (uint8_t*)"Usage: moistcal 1|2|both\r\n", 27, HAL_MAX_DELAY);
        return;
    }

    uint8_t calibrate_m1 = 0, calibrate_m2 = 0;

    if (strcmp(argv[1], "1") == 0) calibrate_m1 = 1;
    else if (strcmp(argv[1], "2") == 0) calibrate_m2 = 1;
    else if (strcmp(argv[1], "both") == 0) calibrate_m1 = calibrate_m2 = 1;
    else {
        HAL_UART_Transmit(&huart6, (uint8_t*)"Invalid arg. Use 1, 2, or both\r\n", 32, HAL_MAX_DELAY);
        return;
    }

    ADC_ChannelConfTypeDef sConfig = {0};
    sConfig.Rank = 1;
    sConfig.SamplingTime = ADC_SAMPLETIME_3CYCLES;

    if (calibrate_m1) {
        HAL_UART_Transmit(&huart6, (uint8_t*)"Confirm sensor M1 is dry, then press ENTER...\r\n", 47, HAL_MAX_DELAY);
        wait_for_enter();

        sConfig.Channel = ADC_CHANNEL_0;
        HAL_ADC_ConfigChannel(&hadc1, &sConfig);
        HAL_ADC_Start(&hadc1);
        HAL_ADC_PollForConversion(&hadc1, HAL_MAX_DELAY);
        m1_cal.dry = HAL_ADC_GetValue(&hadc1);

        HAL_UART_Transmit(&huart6, (uint8_t*)"Confirm sensor M1 is wet, then press ENTER...\r\n", 47, HAL_MAX_DELAY);
        wait_for_enter();

        HAL_ADC_Start(&hadc1);
        HAL_ADC_PollForConversion(&hadc1, HAL_MAX_DELAY);
        m1_cal.wet = HAL_ADC_GetValue(&hadc1);

        snprintf(msg, sizeof(msg), "M1 calibration done: Dry=%lu  Wet=%lu\r\n", m1_cal.dry, m1_cal.wet);
        HAL_UART_Transmit(&huart6, (uint8_t*)msg, strlen(msg), HAL_MAX_DELAY);
    }

    if (calibrate_m2) {
        HAL_UART_Transmit(&huart6, (uint8_t*)"Confirm sensor M2 is dry, then press ENTER...\r\n", 47, HAL_MAX_DELAY);
        wait_for_enter();

        sConfig.Channel = ADC_CHANNEL_1;
        HAL_ADC_ConfigChannel(&hadc1, &sConfig);
        HAL_ADC_Start(&hadc1);
        HAL_ADC_PollForConversion(&hadc1, HAL_MAX_DELAY);
        m2_cal.dry = HAL_ADC_GetValue(&hadc1);

        HAL_UART_Transmit(&huart6, (uint8_t*)"Confirm sensor M2 is wet, then press ENTER...\r\n", 47, HAL_MAX_DELAY);
        wait_for_enter();

        HAL_ADC_Start(&hadc1);
        HAL_ADC_PollForConversion(&hadc1, HAL_MAX_DELAY);
        m2_cal.wet = HAL_ADC_GetValue(&hadc1);

        snprintf(msg, sizeof(msg), "M2 calibration done: Dry=%lu  Wet=%lu\r\n", m2_cal.dry, m2_cal.wet);
        HAL_UART_Transmit(&huart6, (uint8_t*)msg, strlen(msg), HAL_MAX_DELAY);
    }
}


