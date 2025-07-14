#ifndef LOG_FLASH_H
#define LOG_FLASH_H

#include <stdint.h>

#define LOG_ENTRY_MAX 256
extern uint32_t flash_log_index;

typedef struct __attribute__((packed)) {
    uint32_t timestamp_ms;
    uint16_t m1;
    uint16_t m2;
    int16_t ads[4];
} log_entry_t;

typedef struct {
    uint16_t dry;
    uint16_t wet;
} calib_t;

extern calib_t m1_cal, m2_cal;


void flash_write_log_entry(const log_entry_t *entry);
void flash_read_log_entry(uint32_t index, log_entry_t *entry);
extern uint32_t flash_log_index;

#endif // LOG_FLASH_H
