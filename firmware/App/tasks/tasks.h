/**
 * tasks.h — FreeRTOS 任务声明
 *
 * 四个任务的入口函数和通信句柄。
 */

#ifndef TASKS_H
#define TASKS_H

#include "cmsis_os2.h"

/* ================================================================
 * 任务入口函数 (由 CubeMX 生成的 freertos.c 调用)
 * ================================================================ */
void StartTaskButton(void *argument);
void StartTaskBG(void *argument);
void StartTaskDisplay(void *argument);
void StartTaskMotor(void *argument);

/* ================================================================
 * 进程间通信句柄 (由 CubeMX 生成)
 * ================================================================ */
extern osMessageQueueId_t QueueBtnEventsHandle;
extern osMessageQueueId_t QueueRenderCmdsHandle;
extern osMessageQueueId_t QueueMotorTargetsHandle;
extern osEventFlagsId_t   EvtMotorHandle;

/* ================================================================
 * 消息结构体定义
 * ================================================================ */

/* 按键事件消息 */
typedef struct {
    uint8_t btn_id;       /* button_id_t */
    uint8_t event;        /* button_event_t */
    uint32_t timestamp;
} btn_msg_t;

/* 渲染指令消息 */
typedef enum {
    RENDER_FULL_SCREEN,
    RENDER_RECT,
    RENDER_TEXT,
    RENDER_ARC,
    RENDER_TICK_MARKS,
    RENDER_METER,
    RENDER_ICON,
    RENDER_PROGRESS_RING,
} render_cmd_t;

typedef struct {
    uint8_t  cmd;         /* render_cmd_t */
    uint16_t param1;      /* x / cx */
    uint16_t param2;      /* y / cy */
    uint16_t param3;      /* w / radius */
    uint16_t param4;      /* h / thickness */
    uint16_t color;
    uint32_t ptr_or_val;  /* const char* 或 uint32_t 值 */
} render_msg_t;

/* 电机目标消息 */
typedef struct {
    float    target_angle;
    uint8_t  move_mode;   /* pointer_move_mode_t */
} motor_msg_t;

#endif /* TASKS_H */
