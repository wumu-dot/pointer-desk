/**
 * rtc_drv.h — RTC 驱动封装
 *
 * 封装 STM32 HAL RTC 操作，提供时间读写、设置、校验。
 */

#ifndef RTC_DRV_H
#define RTC_DRV_H

#include <stdint.h>
#include <stdbool.h>

/* ================================================================
 * 时间/日期结构体
 * ================================================================ */
typedef struct {
    uint8_t hours;
    uint8_t minutes;
    uint8_t seconds;
} rtc_time_t;

typedef struct {
    uint8_t day;
    uint8_t month;
    uint16_t year;
    uint8_t weekday;  /* 1=Mon, 7=Sun (ISO) */
} rtc_date_t;

typedef struct {
    rtc_time_t time;
    rtc_date_t date;
} rtc_datetime_t;

/* ================================================================
 * 12/24 小时制
 * ================================================================ */
typedef enum {
    RTC_FORMAT_24H,
    RTC_FORMAT_12H,
} rtc_format_t;

/* ================================================================
 * API
 * ================================================================ */
void rtc_drv_init(void);

/* 读写 */
rtc_datetime_t rtc_drv_get_datetime(void);
void rtc_drv_set_datetime(const rtc_datetime_t *dt);

/* 12/24 小时制转换 */
uint8_t rtc_drv_get_hour12(void);       /* 12小时制的小时数 */
bool rtc_drv_is_pm(void);               /* 12小时制判断上下午 */

/* 格式设置 */
void rtc_drv_set_format(rtc_format_t fmt);
rtc_format_t rtc_drv_get_format(void);

/* 有效性检查 */
bool rtc_drv_is_valid(void);            /* RTC 是否已配置 (非首次上电) */

/* 星期文字 */
const char* rtc_drv_weekday_str(uint8_t weekday);  /* 中文星期 */

#endif /* RTC_DRV_H */
