<<<<<<< HEAD
/*
 * Header for the RTC driver. Defines rtc_time_t: second, minute, hour, day,
 * month, year.
 */
=======
>>>>>>> origin/x86_64-uefi
#pragma once
#include <stdint.h>

typedef struct {
    uint8_t second, minute, hour;
    uint8_t day, month;
    uint16_t year;
} rtc_time_t;

rtc_time_t rtc_read(void);