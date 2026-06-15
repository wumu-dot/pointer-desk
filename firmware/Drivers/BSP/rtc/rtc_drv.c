/**
 * rtc_drv.c — RTC 驱动封装实现
 *
 * 封装 STM32 HAL RTC 操作，提供时间读写、设置、校验，
 * 以及 12/24 小时制转换和中文星期名称。
 */

#include "rtc_drv.h"
#include "main.h"
#include "pin_config.h"
#include "app_config.h"

/* ================================================================
 * HAL 句柄 (定义于 main.c)
 * ================================================================ */
extern RTC_HandleTypeDef hrtc;

/* ================================================================
 * 备份寄存器定义
 * ================================================================ */
#define RTC_BKP_MAGIC           0x55AA   /* RTC_BKP_DR1 — RTC 已配置标记 */
#define RTC_BKP_DR_FORMAT       RTC_BKP_DR2  /* 12/24 小时制存储 */

/* ================================================================
 * 静态状态变量
 * ================================================================ */
static bool rtc_valid = false;

/* 格式默认 24H，init 时从备份寄存器恢复 */
static rtc_format_t current_format = RTC_FORMAT_24H;

/* 中文星期名称查找表 (索引 0-6 对应周一~周日) */
static const char *weekday_names[] = {
    "周一",  /* 1 */
    "周二",  /* 2 */
    "周三",  /* 3 */
    "周四",  /* 4 */
    "周五",  /* 5 */
    "周六",  /* 6 */
    "周日",  /* 7 */
};

/* ================================================================
 * 内部辅助
 * ================================================================ */

/**
 * @brief  从备份寄存器读取保存的格式值。
 * @return 12 或 24，异常时返回 24。
 */
static uint32_t rtc_bkp_read_format(void)
{
    uint32_t val = HAL_RTCEx_BKUPRead(&hrtc, RTC_BKP_DR_FORMAT);
    if (val == 12 || val == 24) {
        return val;
    }
    return 24;  /* 默认 */
}

/**
 * @brief  将当前格式写入备份寄存器。
 */
static void rtc_bkp_write_format(rtc_format_t fmt)
{
    uint32_t val = (fmt == RTC_FORMAT_12H) ? 12 : 24;
    HAL_RTCEx_BKUPWrite(&hrtc, RTC_BKP_DR_FORMAT, val);
}

/* ================================================================
 * API 实现
 * ================================================================ */

/**
 * @brief  RTC 驱动初始化。
 *
 * 检查备份寄存器判断 RTC 是否已配置：
 *  - 若 RTC_BKP_DR1 == 0x55AA → 已配置，rtc_valid = true
 *  - 否则 → 首次上电，写入标记并标记无效
 *
 * 同时从 RTC_BKP_DR2 恢复 12/24 小时制设置。
 */
void rtc_drv_init(void)
{
    uint32_t bkp_val = HAL_RTCEx_BKUPRead(&hrtc, RTC_BKP_DR1);

    if (bkp_val == RTC_BKP_MAGIC) {
        rtc_valid = true;
        LOG("RTC: 已配置，时间有效");
    } else {
        rtc_valid = false;
        HAL_RTCEx_BKUPWrite(&hrtc, RTC_BKP_DR1, RTC_BKP_MAGIC);
        LOG("RTC: 首次上电，时间未设置");
    }

    /* 恢复格式 */
    uint32_t fmt_val = rtc_bkp_read_format();
    current_format = (fmt_val == 12) ? RTC_FORMAT_12H : RTC_FORMAT_24H;
    LOG("RTC: 格式 = %s", (current_format == RTC_FORMAT_12H) ? "12H" : "24H");
}

/**
 * @brief  获取当前日期时间。
 * @return rtc_datetime_t 结构体，包含时间和日期。
 *
 * 注意：调用顺序必须为先 GetTime 后 GetDate，
 *       否则 HAL 内部 shadow 寄存器不会锁定到同一时刻。
 */
rtc_datetime_t rtc_drv_get_datetime(void)
{
    rtc_datetime_t dt = {0};

    RTC_TimeTypeDef sTime = {0};
    RTC_DateTypeDef sDate = {0};

    HAL_RTC_GetTime(&hrtc, &sTime, RTC_FORMAT_BIN);
    HAL_RTC_GetDate(&hrtc, &sDate, RTC_FORMAT_BIN);

    dt.time.hours   = sTime.Hours;
    dt.time.minutes = sTime.Minutes;
    dt.time.seconds = sTime.Seconds;

    dt.date.day     = sDate.Date;
    dt.date.month   = sDate.Month;
    dt.date.year    = sDate.Year + 2000;
    dt.date.weekday = sDate.WeekDay;  /* 1=Mon, 7=Sun */

    return dt;
}

/**
 * @brief  设置日期时间。
 * @param  dt  指向 rtc_datetime_t 结构体的指针，包含目标时间。
 *
 * 设置后自动标记 rtc_valid = true。
 */
void rtc_drv_set_datetime(const rtc_datetime_t *dt)
{
    if (dt == NULL) {
        return;
    }

    RTC_TimeTypeDef sTime = {0};
    RTC_DateTypeDef sDate = {0};

    sTime.Hours          = dt->time.hours;
    sTime.Minutes        = dt->time.minutes;
    sTime.Seconds        = dt->time.seconds;
    sTime.DayLightSaving = RTC_DAYLIGHTSAVING_NONE;
    sTime.StoreOperation = RTC_STOREOPERATION_SET;

    sDate.WeekDay = dt->date.weekday;
    sDate.Month   = dt->date.month;
    sDate.Date    = dt->date.day;
    sDate.Year    = dt->date.year - 2000;

    HAL_RTC_SetTime(&hrtc, &sTime, RTC_FORMAT_BIN);
    HAL_RTC_SetDate(&hrtc, &sDate, RTC_FORMAT_BIN);

    rtc_valid = true;
}

/**
 * @brief  获取 12 小时制的小时数。
 * @return 1-12 范围内的小时数。
 *
 * 转换规则：0→12, 13→1, 14→2, ... 23→11。
 */
uint8_t rtc_drv_get_hour12(void)
{
    rtc_datetime_t dt = rtc_drv_get_datetime();
    uint8_t h = dt.time.hours % 12;
    if (h == 0) {
        h = 12;
    }
    return h;
}

/**
 * @brief  判断当前是否为下午 (PM)。
 * @return true 表示 12:00-23:59，false 表示 0:00-11:59。
 */
bool rtc_drv_is_pm(void)
{
    rtc_datetime_t dt = rtc_drv_get_datetime();
    return dt.time.hours >= 12;
}

/**
 * @brief  设置 12 或 24 小时制格式。
 * @param  fmt  RTC_FORMAT_12H 或 RTC_FORMAT_24H。
 *
 * 格式会持久化到备份寄存器，掉电不丢失。
 */
void rtc_drv_set_format(rtc_format_t fmt)
{
    if (fmt != RTC_FORMAT_12H && fmt != RTC_FORMAT_24H) {
        return;
    }
    current_format = fmt;
    rtc_bkp_write_format(fmt);
}

/**
 * @brief  获取当前的 12/24 小时制设置。
 * @return 当前格式枚举值。
 */
rtc_format_t rtc_drv_get_format(void)
{
    return current_format;
}

/**
 * @brief  检查 RTC 是否已配置 (时间是否有效)。
 * @return true 表示 RTC 已被设置过至少一次。
 */
bool rtc_drv_is_valid(void)
{
    return rtc_valid;
}

/**
 * @brief  获取中文星期名称。
 * @param  weekday  星期几 (1=周一, 7=周日)
 * @return 指向中文字符串的指针。
 *
 * 若输入超出范围，返回空字符串。
 */
const char* rtc_drv_weekday_str(uint8_t weekday)
{
    if (weekday < 1 || weekday > 7) {
        return "";
    }
    return weekday_names[weekday - 1];
}
