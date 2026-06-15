/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * File Name          : freertos.c
  * Description        : Code for freertos applications
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2026 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */
/* USER CODE END Header */

/* Includes ------------------------------------------------------------------*/
#include "FreeRTOS.h"
#include "task.h"
#include "main.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include "main.h"
#include "cmsis_os2.h"
#include "tasks.h"
#include "button.h"
#include "mode_manager.h"
#include "pointer_engine.h"
#include "a4988.h"
#include "st7735.h"
#include "gui.h"
#include "fs_mgr.h"
#include "pin_config.h"
#include "app_config.h"
#include "temp_sensor.h"
#include "rtc_drv.h"
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */

/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
/* USER CODE BEGIN Variables */

/* USER CODE END Variables */

/* Private function prototypes -----------------------------------------------*/
/* USER CODE BEGIN FunctionPrototypes */

/* USER CODE END FunctionPrototypes */

/* Private application code --------------------------------------------------*/
/* USER CODE BEGIN Application */

/* ================================================================
 * 按键任务 — 10ms 周期轮询 GPIO, 去抖, 发送事件到后台队列
 * ================================================================ */
void StartTaskButton(void *argument) {
  (void)argument;
  button_init();

  for (;;) {
    button_scan();

    while (button_has_event()) {
      button_msg_t evt = button_get_event();
      btn_msg_t msg = {
        .btn_id    = (uint8_t)evt.id,
        .event     = (uint8_t)evt.event,
        .timestamp = evt.timestamp_ms,
      };
      osMessageQueuePut(QueueBtnEventsHandle, &msg, 0, 0);
    }

    osDelay(pdMS_TO_TICKS(BTN_SCAN_MS));
  }
}

/* ================================================================
 * 后台任务 — 事件驱动 + 周期更新 (100ms)
 *   1. 处理按键事件 → mode_manager
 *   2. 更新当前模式
 *   3. 触发渲染
 *   4. 发送电机目标到电机队列
 * ================================================================ */
void StartTaskBG(void *argument) {
  (void)argument;

  /* 延迟初始化 — 需要 FreeRTOS 运行 (LittleFS, etc.) */
  fs_mgr_init();
  LOG("Background task started, FS ready\r\n");

  uint32_t last_update = osKernelGetTickCount();

  for (;;) {
    /* 1. 处理按键事件 */
    btn_msg_t btn_msg;
    while (osMessageQueueGet(QueueBtnEventsHandle, &btn_msg, NULL, 0) == osOK) {
      mode_manager_handle_button((button_id_t)btn_msg.btn_id,
                                  (button_event_t)btn_msg.event);
    }

    /* 2. 周期性更新 (100ms) */
    uint32_t now = osKernelGetTickCount();
    if (now - last_update >= pdMS_TO_TICKS(100)) {
      last_update = now;
      mode_manager_update();
    }

    /* 3. 渲染请求 — 当前模式绘制到屏幕 */
    mode_manager_render();

    /* 4. 脏矩形 → 显示任务 */
    if (gui_dirty_get_count() > 0) {
      dirty_rect_t merged = gui_dirty_merge();
      if (merged.valid) {
        render_msg_t cmd = {
          .cmd    = RENDER_RECT,
          .param1 = merged.x,
          .param2 = merged.y,
          .param3 = merged.w,
          .param4 = merged.h,
          .color  = 0,
          .ptr_or_val = 0,
        };
        osMessageQueuePut(QueueRenderCmdsHandle, &cmd, 0, 0);
      }
      gui_dirty_clear();
    }

    osDelay(pdMS_TO_TICKS(20)); /* 50Hz 循环 */
  }
}

/* ================================================================
 * 显示任务 — 30fps, 接收渲染指令, 局部刷新 TFT
 *   (当前模式已直接写入 TFT, 此任务保留为扩展点)
 * ================================================================ */
void StartTaskDisplay(void *argument) {
  (void)argument;

  for (;;) {
    render_msg_t cmd;
    if (osMessageQueueGet(QueueRenderCmdsHandle, &cmd, NULL, 0) == osOK) {
      /* 当前架构下, 渲染已由 mode_manager_render() 直接完成。
       * 显示任务负责协调刷新时序, 未来可加入双缓冲/垂直同步。 */
      (void)cmd;
    }
    osDelay(pdMS_TO_TICKS(DISPLAY_REFRESH_MS));
  }
}

/* ================================================================
 * 电机任务 — 50ms 周期
 *   接收目标角度 → 指针引擎更新 → A4988 STEP 脉冲输出
 * ================================================================ */
void StartTaskMotor(void *argument) {
  (void)argument;

  for (;;) {
    /* 非阻塞检查: 有无新模式发来的目标角度 */
    motor_msg_t target;
    if (osMessageQueueGet(QueueMotorTargetsHandle, &target, NULL, 0) == osOK) {
      pointer_set_target(target.target_angle, (pointer_move_mode_t)target.move_mode);
    }

    /* 每 50ms 无条件驱动指针引擎平滑插值 */
    pointer_engine_update();

    /* 角度 → 微步数 → A4988 控制 */
    float angle = pointer_get_current_angle();
    int32_t target_steps = (int32_t)(angle * MOTOR_TOTAL_STEPS / 360.0f);

    motor_state_t ms = a4988_get_state();
    int32_t diff = target_steps - ms.current_steps;

    if (diff > 10) {
      a4988_set_direction(true);
      a4988_set_speed(MOTOR_MAX_SPEED);
    } else if (diff < -10) {
      a4988_set_direction(false);
      a4988_set_speed(MOTOR_MAX_SPEED);
    } else {
      a4988_set_speed(0);
      osEventFlagsSet(EvtMotorHandle, 0x01);
    }

    osDelay(pdMS_TO_TICKS(POINTER_UPDATE_MS));
  }
}

/* USER CODE END Application */

