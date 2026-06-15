/**
 * app_config.h — 应用层配置
 *
 * 所有可调的运行参数集中管理。
 */

#ifndef APP_CONFIG_H
#define APP_CONFIG_H

#include <stdint.h>

/* ================================================================
 * 显示
 * ================================================================ */
#define DISPLAY_REFRESH_MS      33      /* 30fps 刷新周期 */
#define LCD_BRIGHTNESS_DEFAULT  80      /* 默认亮度 80% */
#define LCD_BRIGHTNESS_STEPS    10      /* 亮度档位数 */
#define LCD_BL_FREQ_HZ          1000    /* 背光 PWM 频率 */

/* ================================================================
 * 指针引擎
 * ================================================================ */
#define POINTER_UPDATE_MS       50      /* 微步更新周期 (50ms) */
#define POINTER_SMOOTH_STEPS    20      /* 平滑插值步数 (1秒=20步) */
#define POINTER_TEMP_REFRESH_MS 5000    /* 温度模式指针刷新周期 */

/* 加减速参数 */
#define POINTER_RAMP_ACCEL      400     /* 加速度 (步/秒²) */
#define POINTER_RAMP_MAX_SPEED  800     /* 最高速度 (步/秒) */

/* ================================================================
 * 传感器
 * ================================================================ */
#define TEMP_READ_INTERVAL_MS   5000    /* 温度采样间隔 */
#define ADC_SAMPLING_TIME       ADC_SAMPLETIME_480CYCLES

/* ================================================================
 * 按键
 * ================================================================ */
#define BTN_SCAN_MS             10      /* 扫描周期 */
#define BTN_DEBOUNCE_MS         20
#define BTN_LONG_MS             500

/* ================================================================
 * 计时器
 * ================================================================ */
#define TIMER_DEFAULT_MINUTES   5       /* 默认倒计时时长 */
#define TIMER_STEP_SHORT_MS     60      /* 短按加减步长 (1分钟) */
#define TIMER_STEP_LONG_MS      600     /* 长按加减步长 (10分钟) */
#define TIMER_URGENT_SECONDS    10      /* 倒计时最后紧急阶段 */
#define TIMER_END_BUZZ_MS       5000    /* 结束提示持续时间 */
#define TIMER_END_VIBRATE_COUNT 3       /* 结束震动次数 */

/* ================================================================
 * RTC
 * ================================================================ */
#define RTC_CALIBRATION_PPM     0       /* RTC 校准值 */

/* ================================================================
 * FreeRTOS
 * ================================================================ */
/* 任务栈大小 (字) */
#define TASK_STACK_BUTTON       (configMINIMAL_STACK_SIZE * 2)
#define TASK_STACK_BG           (configMINIMAL_STACK_SIZE * 4)
#define TASK_STACK_DISPLAY      (configMINIMAL_STACK_SIZE * 4)
#define TASK_STACK_MOTOR        (configMINIMAL_STACK_SIZE * 2)

/* 任务优先级 (数字越大越高) */
#define TASK_PRIO_BUTTON        osPriorityHigh
#define TASK_PRIO_BG            osPriorityNormal
#define TASK_PRIO_DISPLAY       osPriorityNormal
#define TASK_PRIO_MOTOR         osPriorityAboveNormal

/* 消息队列大小 */
#define QUEUE_BTN_EVENTS        8       /* 按键事件队列 */
#define QUEUE_RENDER_CMDS       16      /* 渲染指令队列 */
#define QUEUE_MOTOR_TARGETS     4       /* 电机目标队列 */

/* ================================================================
 * LittleFS
 * ================================================================ */
#define LFS_READ_SIZE           256
#define LFS_PROG_SIZE           256
#define LFS_BLOCK_SIZE          4096
#define LFS_BLOCK_COUNT         2048    /* 8MB / 4096 */
#define LFS_CACHE_SIZE          256
#define LFS_LOOKAHEAD_SIZE      16

/* ================================================================
 * 调试
 * ================================================================ */
#define DEBUG_ENABLED           1       /* 调试开关 */
#define DEBUG_UART_BAUD         115200

#if DEBUG_ENABLED
    #include <stdio.h>
    #include "main.h"   /* huart1 */
    extern UART_HandleTypeDef huart1;

    static inline void log_uart_send(const char *str, uint16_t len) {
        HAL_UART_Transmit(&huart1, (uint8_t *)str, len, 100);
    }

    /* LOG: 用户消息 + 自动补 \r\n */
    #define LOG(...) do { \
        char _buf[128]; \
        int _n = snprintf(_buf, sizeof(_buf), "[LOG] " __VA_ARGS__); \
        if (_n > 0) { \
            while (_n > 0 && (_buf[_n-1]=='\r' || _buf[_n-1]=='\n')) _buf[--_n] = '\0'; \
            _buf[_n]='\r'; _buf[_n+1]='\n'; _buf[_n+2]='\0'; \
            log_uart_send(_buf, (uint16_t)(_n+2)); \
        } \
    } while(0)

    /* LOG_ERR: 先格式化用户消息，再追加文件行号，最后补 \r\n */
    #define LOG_ERR(...) do { \
        char _buf[128]; \
        int _n = snprintf(_buf, sizeof(_buf), "[ERR] " __VA_ARGS__); \
        if (_n > 0 && _n < 100) \
            _n += snprintf(_buf + _n, sizeof(_buf) - _n, " (%s:%d)", __FILE__, __LINE__); \
        if (_n > 0) { \
            while (_n > 0 && (_buf[_n-1]=='\r' || _buf[_n-1]=='\n')) _buf[--_n] = '\0'; \
            _buf[_n]='\r'; _buf[_n+1]='\n'; _buf[_n+2]='\0'; \
            log_uart_send(_buf, (uint16_t)(_n+2)); \
        } \
    } while(0)
#else
    #define LOG(fmt, ...)
    #define LOG_ERR(fmt, ...)
#endif

#endif /* APP_CONFIG_H */
