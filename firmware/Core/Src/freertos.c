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
#include "weather_bridge.h"
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
    /* 0. ESP32 UART 天气数据 DMA 轮询 */
    weather_bridge_poll_dma();

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

    /* 3. 渲染请求 — 写入帧缓冲 (仅渲染变化区域, 每区单独标记) */
    mode_manager_render();

    /* 4. 逐脏矩形 flush (每个<5ms, 撕裂线不可见) */
    if (gui_dirty_get_count() > 0) {
      const dirty_rect_t *rects = gui_dirty_get_all();
      for (uint8_t i = 0; i < MAX_DIRTY_RECTS; i++) {
        if (rects[i].valid) {
          st7735_flush_rect(rects[i].x, rects[i].y, rects[i].w, rects[i].h);
        }
      }
      gui_dirty_clear();
    }

    osDelay(pdMS_TO_TICKS(50)); /* 20Hz 循环 */
  }
}

/* ================================================================
 * 显示任务 — 闲置 (帧缓冲刷新已由 BG 任务完成)
 * ================================================================ */
void StartTaskDisplay(void *argument) {
  (void)argument;

  for (;;) {
    /* 帧缓冲模式下, BG 任务已负责刷新。
     * 此任务保留供未来双缓冲/动画扩展。 */
    osDelay(pdMS_TO_TICKS(1000));
  }
}

/* ================================================================
 * 电机任务 — 50ms 周期
 *   接收目标角度 → 指针引擎更新 → A4988 STEP 脉冲输出
 * ================================================================ */
void StartTaskMotor(void *argument) {
  (void)argument;

  /* ---- 启动时使能电机 ---- */
  a4988_enable(true);

  /* ---- 启动自检：低速旋转验证接线 ---- */
  osDelay(pdMS_TO_TICKS(500));
  a4988_set_direction(true);
  a4988_set_speed(50);
  LOG("Motor self-test: CW 50steps/s 2s");
  osDelay(pdMS_TO_TICKS(2000));
  a4988_set_direction(false);
  LOG("Motor self-test: CCW 50steps/s 2s");
  osDelay(pdMS_TO_TICKS(2000));
  a4988_set_speed(0);
  a4988_enable(false);                        /* 自检结束，释放电机 */
  /* 同步当前位置到指针角度，避免正常循环立即全速运转 */
  {
    float angle = pointer_get_current_angle();
    int32_t steps = (int32_t)(angle * MOTOR_TOTAL_STEPS / 360.0f);
    a4988_update_step_count(steps);            /* 覆盖 current_steps=0 */
  }
  LOG("Motor self-test: complete");
  /* ---- 自检结束 ---- */
  (void)argument;                              /* suppress unused warning */

  for (;;) {
    motor_msg_t target;
    if (osMessageQueueGet(QueueMotorTargetsHandle, &target, NULL, 0) == osOK) {
      pointer_set_target(target.target_angle, (pointer_move_mode_t)target.move_mode);
    }

    pointer_engine_update();

    float angle = pointer_get_current_angle();
    int32_t target_steps = (int32_t)(angle * MOTOR_TOTAL_STEPS / 360.0f);
    int32_t diff = target_steps - a4988_get_state().current_steps;

    if (diff > 10) {
      a4988_enable(true);
      a4988_set_direction(true);
      a4988_set_speed(MOTOR_MAX_SPEED);
      a4988_update_step_count((MOTOR_MAX_SPEED * POINTER_UPDATE_MS) / 1000);
    } else if (diff < -10) {
      a4988_enable(true);
      a4988_set_direction(false);
      a4988_set_speed(MOTOR_MAX_SPEED);
      a4988_update_step_count(-(MOTOR_MAX_SPEED * POINTER_UPDATE_MS) / 1000);
    } else {
      a4988_set_speed(0);
      a4988_enable(false);
      osEventFlagsSet(EvtMotorHandle, 0x01);
    }

    osDelay(pdMS_TO_TICKS(POINTER_UPDATE_MS));
  }
}

/* USER CODE END Application */

