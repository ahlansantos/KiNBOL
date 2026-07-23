#pragma once
#include <stdint.h>

typedef struct {
    uint8_t second, minute, hour;
    uint8_t day, month;
    uint16_t year;
} rtc_time_t;

rtc_time_t rtc_read(void);