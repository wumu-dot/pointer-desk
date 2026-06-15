/**
 * temp_adc.c — STM32F4 内部 ADC 温度传感器实现
 *
 * 使用 ADC1 的内部温度传感器通道 (ADC_CHANNEL_TEMPSENSOR / CH16)，
 * 通过 STM32F4 参考手册中的公式将 ADC 读数转换为摄氏度。
 *
 * 精度: ±5°C (典型值，未校准)
 *
 * 依赖:
 *   - main.h        → HAL 外设句柄 (hadc1)
 *   - pin_config.h  → TEMP_ADC, TEMP_ADC_CHANNEL 宏
 *   - app_config.h  → LOG 宏, ADC_SAMPLING_TIME
 */

#include "temp_sensor.h"
#include "main.h"
#include "pin_config.h"
#include "app_config.h"

/* ---- HAL 句柄 (在 main.c 中定义) ---- */
extern ADC_HandleTypeDef hadc1;

/* ---- 内部常量 ---- */
#define TEMP_ADC_RESOLUTION     4096U       /* 12-bit ADC */
#define TEMP_VREF               3.3f        /* 参考电压 */
#define TEMP_V_25               0.76f       /* 25°C 时的电压 (V) */
#define TEMP_AVG_SLOPE          0.0025f     /* 平均斜率 (V/°C) */
#define TEMP_OFFSET_C           25.0f       /* 基准温度偏移 */

/* ---- 模块级状态 ---- */
static bool initialized = false;

/* ================================================================
 * 公开接口
 * ================================================================ */

/**
 * @brief 初始化 ADC 温度传感器
 *
 * 配置 ADC1 通道用于内部温度传感器采样。
 * 实际校准和通道配置由 CubeMX 生成的 HAL 初始化代码完成，
 * 此处仅验证 ADC 句柄有效并标记就绪。
 */
void temp_adc_init(void)
{
    /* ADC 外设已由 HAL_ADC_Init() 在 main.c 中初始化。
     * 此处只需确认句柄可用并记录初始状态。 */
    if (hadc1.Instance == NULL) {
        LOG("[ADC] hadc1.Instance is NULL, ADC not configured");
        initialized = false;
        return;
    }

    /* 如果 CubeMX 未使能内部温度传感器通道，给出警告但不阻止启动 */
    if (TEMP_ADC_CHANNEL != ADC_CHANNEL_TEMPSENSOR) {
        LOG("[ADC] TEMP_ADC_CHANNEL is not ADC_CHANNEL_TEMPSENSOR, check pin_config.h");
    }

    initialized = true;
    LOG("[ADC] Internal temperature sensor ready");
}

/**
 * @brief 读取内部 ADC 温度 (摄氏度)
 *
 * 流程:
 *   1. 选择 ADC 通道
 *   2. 启动单次转换
 *   3. 等待转换完成
 *   4. 读取结果，通过 STM32F4 公式换算温度
 *
 * @return 温度值 (°C)，若未初始化或转换失败返回 0.0f
 */
float temp_adc_read_celsius(void)
{
    HAL_StatusTypeDef status;
    uint32_t adc_val;
    float v_sense;
    float temperature;

    if (!initialized) {
        return 0.0f;
    }

    /* 配置通道并启动转换 */
    ADC_ChannelConfTypeDef sConfig = {0};
    sConfig.Channel      = TEMP_ADC_CHANNEL;
    sConfig.Rank         = 1;
    sConfig.SamplingTime = ADC_SAMPLING_TIME;
    sConfig.Offset       = 0;

    if (HAL_ADC_ConfigChannel(&hadc1, &sConfig) != HAL_OK) {
        return 0.0f;
    }

    /* 启动 ADC */
    status = HAL_ADC_Start(&hadc1);
    if (status != HAL_OK) {
        return 0.0f;
    }

    /* 等待转换完成 (阻塞, 超时 10ms) */
    status = HAL_ADC_PollForConversion(&hadc1, 10);
    if (status != HAL_OK) {
        HAL_ADC_Stop(&hadc1);
        return 0.0f;
    }

    /* 读取结果 */
    adc_val = HAL_ADC_GetValue(&hadc1);

    /* 停止 ADC */
    HAL_ADC_Stop(&hadc1);

    /* ---- STM32F4 内部温度传感器公式 ----
     *
     * V_25    = 0.76V   (25°C 时传感器输出电压)
     * Slope   = 2.5mV/°C (每度电压变化)
     * Vref    = 3.3V    (ADC 参考电压)
     *
     * V_sense = adc_val * Vref / 4096
     * Temp    = (V_sense - V_25) / Slope + 25.0
     */
    v_sense      = (float)adc_val * TEMP_VREF / (float)TEMP_ADC_RESOLUTION;
    temperature  = (v_sense - TEMP_V_25) / TEMP_AVG_SLOPE + TEMP_OFFSET_C;

    return temperature;
}
