/**
 * a4988.c — A4988 步进电机驱动实现
 *
 * 通过 TIM2 CH1 PWM 产生 STEP 脉冲，硬件自动输出不占 CPU。
 * 支持微步细分、方向控制、紧急停止、振动反馈等功能。
 *
 * 硬件连接：
 *   PA0 (AF)  → STEP  (TIM2_CH1 PWM)
 *   PA1 (OUT) → DIR   (HIGH=CW, LOW=CCW)
 *   PA2 (OUT) → EN    (LOW=使能, HIGH=禁用, 低有效)
 *   PA3 (OUT) → MS1/MS2/MS3 (跳线并接, 仅全步/16细分可选)
 *
 * PWM 参数：
 *   APB1 定时器时钟 = 84MHz, TIM2 预分频器 = 83 → 计数器 @ 1MHz
 *   ARR = 1,000,000 / steps_per_sec - 1
 *   CCR = 2  (2μs 脉冲宽度，A4988 要求 ≥1μs)
 */

/* ================================================================
 * 头文件
 * ================================================================ */
#include "a4988.h"
#include "main.h"
#include "pin_config.h"
#include "app_config.h"

#include <string.h>

/* ================================================================
 * 外部句柄
 * ================================================================ */
extern TIM_HandleTypeDef htim2;

/* ================================================================
 * 模块内部状态
 * ================================================================ */
static motor_state_t motor;

/* ================================================================
 * 内部辅助
 * ================================================================ */
static void motor_pwm_stop(void)
{
    /* 先清零 CCR，确保 PA0 (STEP) 输出为低电平，避免悬空或高电平静态功耗 */
    __HAL_TIM_SET_COMPARE(&htim2, MOTOR_STEP_CHANNEL, 0);
    HAL_TIM_PWM_Stop(&htim2, MOTOR_STEP_CHANNEL);
}

static void motor_pwm_start(void)
{
    HAL_TIM_PWM_Start(&htim2, MOTOR_STEP_CHANNEL);
}

/* ----------------------------------------------------------------
 * 根据 step/s 计算 ARR 并保证脉冲宽度安全
 * ---------------------------------------------------------------- */
static uint32_t calc_arr(uint32_t steps_per_sec)
{
    uint32_t arr;

    if (steps_per_sec == 0) {
        return 0;
    }

    arr = 1000000UL / steps_per_sec;       /* 周期 (μs) */
    if (arr < 3) {
        arr = 3;                            /* 留出 CCR=2 的宽度 */
    }
    return arr - 1;                         /* ARR = 周期 - 1 */
}

/* ================================================================
 * 公开 API
 * ================================================================ */

/* ----------------------------------------------------------------
 * a4988_init — 初始化 A4988 驱动
 *
 * 初始化顺序：
 *   1. 清零内部状态 & 填入默认限值
 *   2. 确保 PWM 输出已停止
 *   3. 设置 EN / DIR / MS 引脚的初始电平
 * ---------------------------------------------------------------- */
void a4988_init(void)
{
    /* ---- 状态 ---- */
    memset(&motor, 0, sizeof(motor));
    motor.max_speed    = MOTOR_MAX_SPEED;
    motor.acceleration = MOTOR_ACCEL;

    /* ---- 停止 PWM ---- */
    motor_pwm_stop();

    /* ---- EN = HIGH → 驱动器禁用 (低有效) ---- */
    HAL_GPIO_WritePin(MOTOR_EN_PORT, MOTOR_EN_PIN, GPIO_PIN_SET);

    /* ---- DIR = LOW → 逆时针 ---- */
    HAL_GPIO_WritePin(MOTOR_DIR_PORT, MOTOR_DIR_PIN, GPIO_PIN_RESET);

    /* ---- MS1/MS2/MS3 → 16 微步 (硬件跳线并接于 PA3) ---- */
    a4988_set_microstep(STEP_SIXTEENTH);

    LOG("A4988: init ok  EN=off  DIR=CCW  microstep=1/16");
}

/* ----------------------------------------------------------------
 * a4988_set_microstep — 设置微步细分模式
 *
 * MS1/MS2/MS3 全部并接到 PA3（硬件跳线）。
 * 因此仅全步 (LOW) 与 16 细分 (HIGH) 可用。
 *
 * mode 为 STEP_SIXTEENTH 时置高，其余一律置低。
 * ---------------------------------------------------------------- */
void a4988_set_microstep(microstep_t mode)
{
    GPIO_PinState state;

    if (mode == STEP_SIXTEENTH) {
        state = GPIO_PIN_SET;
    } else {
        state = GPIO_PIN_RESET;
    }

    /* 三根 MS 线物理连接至同一引脚，只需操作一次 */
    HAL_GPIO_WritePin(MOTOR_MS1_PORT, MOTOR_MS1_PIN, state);
}

/* ----------------------------------------------------------------
 * a4988_enable — 使能 / 禁用电机驱动器
 *
 * en = true  → EN = LOW  (驱动器使能，电机通电)
 * en = false → EN = HIGH (驱动器禁用，释放电机)
 * ---------------------------------------------------------------- */
void a4988_enable(bool en)
{
    GPIO_PinState level = en ? GPIO_PIN_RESET : GPIO_PIN_SET;
    HAL_GPIO_WritePin(MOTOR_EN_PORT, MOTOR_EN_PIN, level);
}

/* ----------------------------------------------------------------
 * a4988_set_direction — 设置旋转方向
 *
 * dir = true  → DIR = HIGH (顺时针)
 * dir = false → DIR = LOW  (逆时针)
 * ---------------------------------------------------------------- */
void a4988_set_direction(bool dir)
{
    motor.direction = dir;
    HAL_GPIO_WritePin(MOTOR_DIR_PORT, MOTOR_DIR_PIN,
                      dir ? GPIO_PIN_SET : GPIO_PIN_RESET);
}

/* ----------------------------------------------------------------
 * a4988_set_speed — 设置 STEP 脉冲频率
 *
 * steps_per_sec = 0  → 停止 PWM (电机停转)
 * steps_per_sec > 0  → 更新 ARR 并启动/维持 PWM 输出
 *
 * 频率超过 max_speed 时自动钳位。
 * 首次启动时调用 HAL_TIM_PWM_Start，已在运行则仅更新寄存器。
 * ---------------------------------------------------------------- */
void a4988_set_speed(uint32_t steps_per_sec)
{
    uint32_t arr;
    uint32_t ccr;

    /* ---- 停止 ---- */
    if (steps_per_sec == 0) {
        motor_pwm_stop();
        motor.is_moving = false;
        return;
    }

    /* ---- 钳位 ---- */
    if (steps_per_sec > motor.max_speed) {
        steps_per_sec = motor.max_speed;
    }

    arr = calc_arr(steps_per_sec);
    ccr = 2;                            /* 2 μs 脉冲 */
    if (ccr >= arr) {
        ccr = arr - 1;                  /* 保证 CCR < ARR */
    }

    __HAL_TIM_SET_AUTORELOAD(&htim2, arr);
    __HAL_TIM_SET_COMPARE(&htim2, MOTOR_STEP_CHANNEL, ccr);

    /* 产生更新事件，使 ARR 立即生效 */
    htim2.Instance->EGR = TIM_EGR_UG;

    if (!motor.is_moving) {
        motor_pwm_start();
        motor.is_moving = true;
    }
}

/* ----------------------------------------------------------------
 * a4988_pulse_once — 发送单个 STEP 脉冲 (调试用)
 *
 * 实现方式：暂停 PWM → 将 PA0 切换为 GPIO 输出 → 产生一个 ~2μs
 * 正脉冲 → 恢复复用功能 → 若之前在运行则重启 PWM。
 *
 * 通过保存/恢复 MODER 寄存器避免硬编码复用功能编号。
 * ---------------------------------------------------------------- */
void a4988_pulse_once(void)
{
    bool was_moving = motor.is_moving;

    if (was_moving) {
        motor_pwm_stop();
    }

    /*
     * 将 PA0 从 AF 模式临时切换为推挽输出。
     * 保存完整的 MODER 寄存器值，恢复时直接写回。
     */
    uint32_t moder_saved = MOTOR_STEP_PORT->MODER;

    MOTOR_STEP_PORT->MODER = (moder_saved & ~GPIO_MODER_MODER0)
                           | GPIO_MODER_MODER0_0;   /* 输出模式 */

    /* ---- 单脉冲 (~2 μs HIGH) ---- */
    MOTOR_STEP_PORT->BSRR = MOTOR_STEP_PIN;          /* STEP = HIGH */
    for (volatile uint32_t d = 0; d < 20; d++) {
        __NOP();
    }
    MOTOR_STEP_PORT->BSRR = (uint32_t)MOTOR_STEP_PIN << 16;  /* STEP = LOW (BSRR high 16 bits = reset) */

    /* ---- 恢复复用功能 ---- */
    MOTOR_STEP_PORT->MODER = moder_saved;

    if (was_moving) {
        motor_pwm_start();
    }
}

/* ----------------------------------------------------------------
 * a4988_emergency_stop — 紧急停止
 *
 * 立即关闭 PWM 输出，清除运行标志。
 * 不释放电机 (EN 保持不变)，以防正在承受负载时掉落。
 * ---------------------------------------------------------------- */
void a4988_emergency_stop(void)
{
    motor_pwm_stop();
    motor.is_moving = false;
}

/* ----------------------------------------------------------------
 * a4988_get_state — 获取电机状态快照
 * ---------------------------------------------------------------- */
motor_state_t a4988_get_state(void)
{
    return motor;
}

/* ----------------------------------------------------------------
 * a4988_update_step_count — 更新当前步数估计值（无编码器补偿）
 *
 * 电机任务每周期调用，依据已运行的 PWM 脉冲数估算当前位置。
 * 无物理编码器，累计误差随长周期增大，接近目标后自动归零。
 * ---------------------------------------------------------------- */
void a4988_update_step_count(int32_t delta)
{
    motor.current_steps += delta;
}

/* ----------------------------------------------------------------
 * a4988_is_moving — 电机是否正在运行
 * ---------------------------------------------------------------- */
bool a4988_is_moving(void)
{
    return motor.is_moving;
}

/* ----------------------------------------------------------------
 * a4988_vibrate — 振动效果 (计时器结束提示)
 *
 * 通过快速切换方向产生触觉反馈，而非真实振动马达。
 * 电机在使能状态下以 200 step/s 速度运行，按指定次数和间隔
 * 反复反转方向，模拟振动。
 *
 * 执行完毕后恢复运行前状态 (方向 / 使能)。
 * ---------------------------------------------------------------- */
void a4988_vibrate(uint8_t count, uint16_t interval_ms)
{
    bool was_moving;
    bool was_dir;
    uint16_t half;
    uint8_t i;

    if (count == 0 || interval_ms == 0) {
        return;
    }

    half = interval_ms / 2;
    if (half < 2) {
        half = 2;
    }

    /* ---- 保存上下文 ---- */
    was_moving = motor.is_moving;
    was_dir    = motor.direction;

    /* ---- 确保电机通电 ---- */
    a4988_enable(true);

    /* ---- 中速运转供振动 ---- */
    a4988_set_speed(200);

    /* ---- 反复切换方向 ---- */
    for (i = 0; i < count; i++) {
        a4988_set_direction(true);
        HAL_Delay(half);
        a4988_set_direction(false);
        HAL_Delay(half);
    }

    /* ---- 停止 PWM ---- */
    a4988_set_speed(0);

    /* ---- 恢复方向 ---- */
    a4988_set_direction(was_dir);

    /* ---- 恢复使能状态 ---- */
    if (!was_moving) {
        a4988_enable(false);
    }
}
