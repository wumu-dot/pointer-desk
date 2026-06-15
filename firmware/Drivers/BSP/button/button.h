/**
 * button.h — 四角开关驱动接口
 *
 * 5向导航开关 (上/下/左/右/按下)，带去抖、短按、长按识别。
 */

#ifndef BUTTON_H
#define BUTTON_H

#include <stdint.h>
#include <stdbool.h>

/* ================================================================
 * 按键 ID
 * ================================================================ */
typedef enum {
    BTN_NONE    = 0,
    BTN_UP,
    BTN_DOWN,
    BTN_LEFT,
    BTN_RIGHT,
    BTN_CENTER,
} button_id_t;

/* ================================================================
 * 按键事件
 * ================================================================ */
typedef enum {
    BTN_EVENT_NONE          = 0,
    BTN_EVENT_SHORT_PRESS,        /* 短按 (<500ms) */
    BTN_EVENT_LONG_PRESS,         /* 长按 (≥500ms) */
    BTN_EVENT_LONG_REPEAT,        /* 长按连发 (每200ms) */
    BTN_EVENT_RELEASE,            /* 释放 */
} button_event_t;

typedef struct {
    button_id_t    id;
    button_event_t event;
    uint32_t       timestamp_ms;
} button_msg_t;

/* ================================================================
 * API
 * ================================================================ */
void button_init(void);
void button_scan(void);                         /* 由 10ms 定时器调用 */

/* 读取去抖后的按键状态 */
button_id_t button_get_pressed(void);
bool button_is_pressed(button_id_t btn);

/* 事件队列 (用于 FreeRTOS 消息队列填充) */
button_msg_t button_get_event(void);
bool button_has_event(void);

#endif /* BUTTON_H */
