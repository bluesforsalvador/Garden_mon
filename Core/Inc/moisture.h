#ifndef MOISTURE_H
#define MOISTURE_H

#include <stdint.h>

typedef struct {
    uint16_t dry;
    uint16_t wet;
} moisture_cal_t;

extern moisture_cal_t m1_cal;
extern moisture_cal_t m2_cal;

#endif // MOISTURE_H
