/**
 * button.c — 四角开关驱动实现 (5向导航键)
 *
 * 上/下/左/右/按下 — 带去抖、短按、长按、长按连发、释放识别。
 * 由 10ms 定时器周期性调用 button_scan()。
 *
 * 依赖:
 *   - main.h       → HAL 外设接口 (HAL_GPIO_ReadPin, HAL_GetTick)
 *   - button.h     → 公有的 ID / 事件枚举 与 API
 *   - pin_config.h → 引脚宏 (BTN_UP_PIN, BTN_UP_PORT 等)
 *   - app_config.h → LOG 宏, BTN_SCAN_MS, BTN_DEBOUNCE_MS, BTN_LONG_MS
 */

#include "button.h"
#include "main.h"
#include "pin_config.h"
#include "app_config.h"

/* ================================================================
 * 本地常量
 * ================================================================ */

#define BTN_COUNT               5
#define DEBOUNCE_SAMPLES        2       /* 连续相同读数次数 → 20ms 有效去抖 */
#define LONG_REPEAT_INTERVAL_MS 200     /* 长按连发间隔 */
#define EVENT_QUEUE_SIZE        16      /* 内部事件队列容量 */

/**
 * 原始电平字面量 (便于阅读)
 *
 * 全部按键配置为 INPUT + PULLUP:
 *   - 按下  = GND → GPIO_PIN_RESET → 逻辑 0
 *   - 释放  = VCC (上拉)     → 逻辑 1
 */
#define RAW_PRESSED   0
#define RAW_RELEASED  1

/* ================================================================
 * 每按键状态机
 * ================================================================ */

typedef struct {
    GPIO_TypeDef *port;
    uint16_t      pin;
    button_id_t   id;

    /* ---- 去抖 ---- */
    uint8_t raw_last;           /* 上一次原始读数 (RAW_PRESSED / RAW_RELEASED) */
    uint8_t stable_cnt;         /* 连续与 raw_last 相同的次数 */
    uint8_t debounced;          /* 去抖后的稳态 (RAW_PRESSED / RAW_RELEASED) */

    /* ---- 长按 / 连发 ---- */
    bool     pressed;           /* 当前是否处于按下稳态 */
    uint32_t press_start_ms;    /* 按下起始时刻 (HAL_GetTick) */
    bool     long_fired;        /* 长按事件是否已触发 (保证只触发一次) */
    uint32_t last_repeat_ms;    /* 上次连发时刻 */
    bool     release_pending;   /* 释放后是否需要发送 BTN_EVENT_RELEASE */
} btn_ctx_t;

/* ================================================================
 * 静态变量
 * ================================================================ */

static btn_ctx_t btn_ctx[BTN_COUNT];

/* 事件环形队列
 *
 * 约定: head == tail 为空; (head+1) % SIZE == tail 为满 (保留一个空位)。
 * 假设 button_scan() 与 button_get_event() 运行在同一任务/临界区内。
 */
static button_msg_t event_queue[EVENT_QUEUE_SIZE];
static volatile uint8_t event_head = 0;
static volatile uint8_t event_tail = 0;

/* ================================================================
 * 内部辅助 — 环形队列
 * ================================================================ */

static inline bool queue_is_full(void)
{
    return ((event_head + 1) % EVENT_QUEUE_SIZE) == event_tail;
}

static inline bool queue_is_empty(void)
{
    return event_head == event_tail;
}

static void queue_push(button_msg_t msg)
{
    if (queue_is_full()) {
        /* 丢弃最旧事件，为新事件腾出空间 */
        event_tail = (event_tail + 1) % EVENT_QUEUE_SIZE;
    }
    event_queue[event_head] = msg;
    event_head = (event_head + 1) % EVENT_QUEUE_SIZE;
}

static button_msg_t queue_pop(void)
{
    button_msg_t msg = { BTN_NONE, BTN_EVENT_NONE, 0 };
    if (!queue_is_empty()) {
        msg = event_queue[event_tail];
        event_tail = (event_tail + 1) % EVENT_QUEUE_SIZE;
    }
    return msg;
}

/* ================================================================
 * 公开 API
 * ================================================================ */

/**
 * @brief 初始化全部 5 路按键上下文，清空事件队列。
 */
void button_init(void)
{
    static const struct {
        GPIO_TypeDef *port;
        uint16_t      pin;
        button_id_t   id;
    } btn_cfg[BTN_COUNT] = {
        { BTN_UP_PORT,     BTN_UP_PIN,     BTN_UP     },
        { BTN_DOWN_PORT,   BTN_DOWN_PIN,   BTN_DOWN   },
        { BTN_LEFT_PORT,   BTN_LEFT_PIN,   BTN_LEFT   },
        { BTN_RIGHT_PORT,  BTN_RIGHT_PIN,  BTN_RIGHT  },
        { BTN_CENTER_PORT, BTN_CENTER_PIN, BTN_CENTER },
    };

    for (int i = 0; i < BTN_COUNT; i++) {
        btn_ctx[i].port            = btn_cfg[i].port;
        btn_ctx[i].pin             = btn_cfg[i].pin;
        btn_ctx[i].id              = btn_cfg[i].id;
        btn_ctx[i].raw_last        = RAW_RELEASED;
        btn_ctx[i].stable_cnt      = 0;
        btn_ctx[i].debounced       = RAW_RELEASED;
        btn_ctx[i].pressed         = false;
        btn_ctx[i].press_start_ms  = 0;
        btn_ctx[i].long_fired      = false;
        btn_ctx[i].last_repeat_ms  = 0;
        btn_ctx[i].release_pending = false;
    }

    event_head = 0;
    event_tail = 0;

    LOG("button: init OK, %d btns, scan=%dms, debounce=%dms, long=%dms",
        BTN_COUNT, BTN_SCAN_MS, BTN_DEBOUNCE_MS, BTN_LONG_MS);
}

/**
 * @brief 按键扫描 — 由 10ms 定时器周期性调用。
 *
 * 每路按键执行:
 *   1. 读取 GPIO 原始电平
 *   2. 软件去抖 (连续 2 次相同读数 → 20ms 有效去抖)
 *   3. 边沿检测 → 按下/释放
 *   4. 按下期间: 长按判定 / 连发
 *   5. 释放时: 短按 / 释放事件入队
 */
void button_scan(void)
{
    uint32_t now = HAL_GetTick();

    for (int i = 0; i < BTN_COUNT; i++) {
        btn_ctx_t *ctx = &btn_ctx[i];

        /* ---- 1. 读取原始电平 ---- */
        uint8_t raw = (HAL_GPIO_ReadPin(ctx->port, ctx->pin) == GPIO_PIN_RESET)
                        ? RAW_PRESSED : RAW_RELEASED;

        /* ---- 2. 软件去抖 ---- */
        if (raw == ctx->raw_last) {
            if (ctx->stable_cnt < DEBOUNCE_SAMPLES) {
                ctx->stable_cnt++;
            }
        } else {
            ctx->stable_cnt = 0;
            ctx->raw_last   = raw;
        }

        /* 未达到去抖阈值，跳过该按键的状态机 */
        if (ctx->stable_cnt < DEBOUNCE_SAMPLES) {
            continue;
        }

        uint8_t old_debounced = ctx->debounced;
        ctx->debounced = raw;

        /* ---- 3. 边沿: 释放 → 按下 ---- */
        if (old_debounced == RAW_RELEASED && ctx->debounced == RAW_PRESSED) {
            ctx->pressed         = true;
            ctx->press_start_ms  = now;
            ctx->long_fired      = false;
            ctx->last_repeat_ms  = 0;
            ctx->release_pending = false;
            continue;
        }

        /* ---- 4. 保持按下: 长按判定 / 连发 ---- */
        if (ctx->pressed && ctx->debounced == RAW_PRESSED) {
            uint32_t elapsed = now - ctx->press_start_ms;

            /* 4a. 长按首次触发 */
            if (!ctx->long_fired && elapsed >= BTN_LONG_MS) {
                button_msg_t msg = { ctx->id, BTN_EVENT_LONG_PRESS, now };
                queue_push(msg);
                ctx->long_fired      = true;
                ctx->release_pending = true;
                ctx->last_repeat_ms  = now;
            }

            /* 4b. 长按连发 (每 200ms) */
            if (ctx->long_fired &&
                (now - ctx->last_repeat_ms) >= LONG_REPEAT_INTERVAL_MS) {
                button_msg_t msg = { ctx->id, BTN_EVENT_LONG_REPEAT, now };
                queue_push(msg);
                ctx->last_repeat_ms = now;
            }
            continue;
        }

        /* ---- 5. 边沿: 按下 → 释放 ---- */
        if (ctx->pressed && ctx->debounced == RAW_RELEASED) {
            /* 5a. 短按 — 未触发过长按即为短按 */
            if (!ctx->long_fired) {
                button_msg_t msg = { ctx->id, BTN_EVENT_SHORT_PRESS, now };
                queue_push(msg);
            }

            /* 5b. 释放事件 — 仅长按后需要 */
            if (ctx->release_pending) {
                button_msg_t msg = { ctx->id, BTN_EVENT_RELEASE, now };
                queue_push(msg);
            }

            /* 复位 */
            ctx->pressed         = false;
            ctx->press_start_ms  = 0;
            ctx->long_fired      = false;
            ctx->last_repeat_ms  = 0;
            ctx->release_pending = false;
            continue;
        }
    }
}

/**
 * @brief 返回当前按下的第一个按键 ID (去抖后)。无按键时返回 BTN_NONE。
 */
button_id_t button_get_pressed(void)
{
    for (int i = 0; i < BTN_COUNT; i++) {
        if (btn_ctx[i].debounced == RAW_PRESSED) {
            return btn_ctx[i].id;
        }
    }
    return BTN_NONE;
}

/**
 * @brief 查询指定按键是否按下 (去抖后)。
 */
bool button_is_pressed(button_id_t btn)
{
    if (btn == BTN_NONE) {
        return false;
    }
    for (int i = 0; i < BTN_COUNT; i++) {
        if (btn_ctx[i].id == btn) {
            return (btn_ctx[i].debounced == RAW_PRESSED);
        }
    }
    return false;
}

/**
 * @brief 从内部事件队列中取出一个事件 (FIFO)。
 *        队列为空时返回 id=BTN_NONE, event=BTN_EVENT_NONE 的消息。
 */
button_msg_t button_get_event(void)
{
    return queue_pop();
}

/**
 * @brief 查询内部事件队列中是否还有未处理的事件。
 */
bool button_has_event(void)
{
    return !queue_is_empty();
}
