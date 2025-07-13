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

#define CLI_BUFFER_SIZE 64
#define MAX_COMMANDS 20
#define MAX_ARGS 10
#define I2C_TIMEOUT 100
#define CLI_INPUT_QUEUE_LEN 1

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
    { "flash", "flash id      - Read JEDEC ID from SPI flash", cmd_flash }
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
    HAL_UART_Receive_IT(&huart6, &rx_byte, 1);

    TickType_t lastUpdate = xTaskGetTickCount();

    while (1) {
        cli_input_t input;
        if (osMessageQueueGet(cli_input_queue, &input, NULL, 0) == osOK) {
            CLI_ProcessCommand(input.line);
            HAL_UART_Transmit(&huart6, (uint8_t*)"\r\n> ", 4, HAL_MAX_DELAY);
        }

        if (xTaskGetTickCount() - lastUpdate >= pdMS_TO_TICKS(1000)) {
            for (uint8_t ch = 0; ch < 4; ch++) {
                uint8_t config[] = {0x01, (uint8_t)(0xC1 | (ch << 4)), 0x83};
                uint8_t pointer = 0x00;
                uint8_t result[2];
                if (HAL_I2C_Master_Transmit(&hi2c1, 0x48 << 1, config, 3, I2C_TIMEOUT) == HAL_OK) {
                    HAL_Delay(10);
                    if (HAL_I2C_Master_Transmit(&hi2c1, 0x48 << 1, &pointer, 1, I2C_TIMEOUT) == HAL_OK &&
                        HAL_I2C_Master_Receive(&hi2c1, 0x48 << 1, result, 2, I2C_TIMEOUT) == HAL_OK) {
                        ads_results[ch] = (result[0] << 8) | result[1];
                    }
                }
            }

            char line0[17], line1[17];
            snprintf(line0, sizeof(line0), "0:%d 1:%d", ads_results[0], ads_results[1]);
            snprintf(line1, sizeof(line1), "2:%d 3:%d", ads_results[2], ads_results[3]);
            st7032_clear();
            st7032_set_cursor(0, 0);
            st7032_write_str(line0);
            st7032_set_cursor(1, 0);
            st7032_write_str(line1);

            lastUpdate = xTaskGetTickCount();
        }

        vTaskDelay(pdMS_TO_TICKS(50));
    }
}

void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart) {
    if (huart->Instance == USART6) {
        uint8_t ch = rx_byte;


        if (escape_state == 0 && ch == 0x1B) {
            escape_state = 1;  // ESC received
        } else if (escape_state == 1 && ch == '[') {
            escape_state = 2;  // [
        } else if (escape_state == 2 && ch == 'A') {
            // Up arrow pressed
            escape_state = 0;
            strncpy(temp_line, last_command, CLI_BUFFER_SIZE);
            temp_index = strlen(last_command);
            HAL_UART_Transmit(&huart6, (uint8_t*)"\r\n> ", 4, HAL_MAX_DELAY);
            HAL_UART_Transmit(&huart6, (uint8_t*)temp_line, temp_index, HAL_MAX_DELAY);
        } else {
            escape_state = 0;
            if (ch == '\r' || ch == '\n') {
                cli_input_t input = {0};
                strncpy(input.line, temp_line, temp_index);
                strncpy(last_command, temp_line, CLI_BUFFER_SIZE);  // Store last
                osMessageQueuePut(cli_input_queue, &input, 0, 0);
                temp_index = 0;
                memset(temp_line, 0, CLI_BUFFER_SIZE);
            } else if (ch == 0x7F || ch == '\b') {
                if (temp_index > 0) {
                    temp_index--;
                    HAL_UART_Transmit(&huart6, (uint8_t*)"\b \b", 3, HAL_MAX_DELAY);
                }
            } else if (temp_index < CLI_BUFFER_SIZE - 1) {
                temp_line[temp_index++] = ch;
                HAL_UART_Transmit(&huart6, &ch, 1, HAL_MAX_DELAY);
            }
        }

        HAL_UART_Receive_IT(&huart6, &rx_byte, 1);
    }
}


static void CLI_ProcessCommand(const char *cmd) {
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
