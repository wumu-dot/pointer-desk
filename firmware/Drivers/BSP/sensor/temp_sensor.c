/**
 * temp_sensor.c — 统一温度传感器接口 + SHT30 I2C 实现
 *
 * 提供对上层透明的温度/湿度读取接口，自动选择最佳可用传感器:
 *   1. SHT30 (外部 I2C, ±0.3°C) — 优先
 *   2. 内部 ADC 温度传感器 (±5°C) — 回退
 *
 * 依赖:
 *   - main.h        → HAL 外设句柄 (hadc1, hi2c2)
 *   - pin_config.h  → TEMP_ADC, SHT30_I2C 等宏
 *   - app_config.h  → LOG 宏, TEMP_READ_INTERVAL_MS
 */

#include "temp_sensor.h"
#include "main.h"
#include "pin_config.h"
#include "app_config.h"

/* ---- HAL 句柄 (在 main.c 中定义) ---- */
extern ADC_HandleTypeDef  hadc1;
extern I2C_HandleTypeDef  hi2c2;

/* ================================================================
 * SHT30 常量 (Datasheet: Sensirion SHT3x-DIS)
 * ================================================================ */

/* I2C 地址: 7-bit 地址左移 1 位 (HAL 要求) */
#define SHT30_I2C_ADDR_7BIT     (SHT30_I2C_ADDR << 1)   /* 0x88 */

/* 测量命令 */
#define SHT30_CMD_SINGLE_SHOT_H 0x2C   /* 单次测量, 高重复性, clock stretching disabled */
#define SHT30_CMD_SINGLE_SHOT_L 0x06

/* 测量等待时间 */
#define SHT30_MEASURE_WAIT_MS   20

/* 数据格式: [T_MSB, T_LSB, T_CRC, H_MSB, H_LSB, H_CRC] */
#define SHT30_RX_DATA_LEN       6
#define SHT30_T_MSB_IDX         0
#define SHT30_T_LSB_IDX         1
#define SHT30_H_MSB_IDX         3
#define SHT30_H_LSB_IDX         4

/* 温度/湿度换算公式 */
#define SHT30_TEMP_MIN          (-45.0f)
#define SHT30_TEMP_RANGE        175.0f
#define SHT30_RAW_MAX           65535.0f
#define SHT30_HUMI_RANGE        100.0f

/* ---- 译注: Datasheet §4.13 换算公式 ----
 * T[°C] = -45 + 175 * (S_T / 65535)
 * RH[%] = 100 * (S_RH / 65535)
 */

/* ================================================================
 * 模块级状态
 * ================================================================ */

/* 全局传感器选择 */
static temp_source_t g_source = TEMP_SRC_NONE;

/* SHT30 探测结果 */
static bool sht30_present = false;

/* ================================================================
 * SHT30 实现
 * ================================================================ */

/**
 * @brief 初始化 SHT30 传感器
 *
 * 通过 HAL_I2C_IsDeviceReady() 探测 I2C 总线上的 SHT30。
 * 探测成功后设置 sht30_present 标志。
 */
void sht30_init(void)
{
    HAL_StatusTypeDef status;

    /* 探测 SHT30: 发送设备地址，检查 ACK */
    status = HAL_I2C_IsDeviceReady(&hi2c2, SHT30_I2C_ADDR_7BIT, 3, 100);
    if (status == HAL_OK) {
        sht30_present = true;
        LOG("[SHT30] Device detected at 0x%02X (I2C2)", SHT30_I2C_ADDR);
    } else {
        sht30_present = false;
        LOG("[SHT30] Device not found at 0x%02X (I2C2)", SHT30_I2C_ADDR);
    }
}

/**
 * @brief 查询 SHT30 是否在 I2C 总线上
 *
 * @return true  传感器已探测到
 * @return false 传感器未连接或无响应
 */
bool sht30_is_present(void)
{
    return sht30_present;
}

/**
 * @brief 从 SHT30 读取温湿度
 *
 * 协议流程:
 *   1. 发送单次测量命令 (0x2C06)
 *   2. 等待 20ms 测量完成
 *   3. 读取 6 字节结果
 *   4. 解析温度/湿度原始值并换算
 *
 * 注意: CRC 校验字节被跳过 (用户说明允许简化)。
 *
 * @return temp_data_t 包含温度、湿度、数据来源和时间戳
 *         若通信失败，temperature/humidity 均为 0.0f, source 为 TEMP_SRC_NONE
 */
temp_data_t sht30_read(void)
{
    temp_data_t data = {0};
    data.source = TEMP_SRC_NONE;

    if (!sht30_present) {
        return data;
    }

    /* 第1步: 发送单次测量命令 */
    uint8_t cmd[2] = { SHT30_CMD_SINGLE_SHOT_H, SHT30_CMD_SINGLE_SHOT_L };
    HAL_StatusTypeDef status = HAL_I2C_Master_Transmit(&hi2c2, SHT30_I2C_ADDR_7BIT, cmd, 2, 100);
    if (status != HAL_OK) {
        LOG("[SHT30] Transmit command failed (status=%d)", status);
        return data;
    }

    /* 第2步: 等待测量完成 */
    HAL_Delay(SHT30_MEASURE_WAIT_MS);

    /* 第3步: 读取 6 字节结果 [T_MSB, T_LSB, T_CRC, H_MSB, H_LSB, H_CRC] */
    uint8_t rx_buf[SHT30_RX_DATA_LEN] = {0};
    status = HAL_I2C_Master_Receive(&hi2c2, SHT30_I2C_ADDR_7BIT, rx_buf, SHT30_RX_DATA_LEN, 100);
    if (status != HAL_OK) {
        LOG("[SHT30] Receive data failed (status=%d)", status);
        return data;
    }

    /* 第4步: 解析原始数据 (跳过 CRC 校验) */
    uint16_t t_raw = ((uint16_t)rx_buf[SHT30_T_MSB_IDX] << 8) | rx_buf[SHT30_T_LSB_IDX];
    uint16_t h_raw = ((uint16_t)rx_buf[SHT30_H_MSB_IDX] << 8) | rx_buf[SHT30_H_LSB_IDX];

    /* 换算温度: T[°C] = -45 + 175 * (S_T / 65535) */
    data.temperature = SHT30_TEMP_MIN + SHT30_TEMP_RANGE * ((float)t_raw / SHT30_RAW_MAX);

    /* 换算湿度: RH[%] = 100 * (S_RH / 65535) */
    data.humidity    = SHT30_HUMI_RANGE * ((float)h_raw / SHT30_RAW_MAX);

    data.source      = TEMP_SRC_SHT30;
    data.timestamp_ms = HAL_GetTick();

    return data;
}

/* ================================================================
 * 统一传感器接口
 * ================================================================ */

/**
 * @brief 初始化温度传感器子系统
 *
 * 初始化顺序:
 *   1. 初始化内部 ADC 温度传感器
 *   2. 探测外部 SHT30
 *   3. 自动选择最佳可用传感器
 *
 * 优先级: SHT30 > 内部 ADC > 无传感器
 */
void temp_sensor_init(void)
{
    /* 初始化 ADC */
    temp_adc_init();

    /* 探测 SHT30 */
    sht30_init();

    /* 自动选择传感器 */
    if (sht30_present) {
        g_source = TEMP_SRC_SHT30;
        LOG("[TEMP] Selected SHT30 (external, ±0.3°C)");
    } else {
        g_source = TEMP_SRC_INTERNAL_ADC;
        LOG("[TEMP] Selected internal ADC (STM32, ±5°C)");
    }
}

/**
 * @brief 读取当前温湿度 (自动路由到最佳传感器)
 *
 * 根据当前选择的传感器调用对应实现:
 *   - SHT30: 返回温度 + 湿度
 *   - ADC:   仅返回温度, humidity = 0
 *
 * @return temp_data_t 传感器数据
 */
temp_data_t temp_sensor_read(void)
{
    temp_data_t data = {0};

    switch (g_source) {
    case TEMP_SRC_SHT30:
        data = sht30_read();
        break;

    case TEMP_SRC_INTERNAL_ADC:
        data.temperature  = temp_adc_read_celsius();
        data.humidity     = 0.0f;
        data.source       = TEMP_SRC_INTERNAL_ADC;
        data.timestamp_ms = HAL_GetTick();
        break;

    case TEMP_SRC_NONE:
    default:
        data.temperature  = 0.0f;
        data.humidity     = 0.0f;
        data.source       = TEMP_SRC_NONE;
        data.timestamp_ms = HAL_GetTick();
        break;
    }

    return data;
}

/**
 * @brief 检查是否连接了外部 SHT30 传感器
 *
 * @return true  SHT30 可用
 * @return false 仅内部 ADC 可用
 */
bool temp_sensor_has_external(void)
{
    return sht30_present;
}

/**
 * @brief 获取当前活动的传感器类型
 *
 * @return temp_source_t 当前传感器枚举值
 */
temp_source_t temp_sensor_get_source(void)
{
    return g_source;
}

/**
 * @brief 获取当前传感器的精度描述标签
 *
 * @return "±0.3°C SHT30"  (外部传感器)
 *         "±5°C 内部"      (内部 ADC)
 *         "无传感器"        (无可用传感器)
 */
const char* temp_sensor_get_label(void)
{
    switch (g_source) {
    case TEMP_SRC_SHT30:
        return "+-0.3C SHT30";
    case TEMP_SRC_INTERNAL_ADC:
        return "+-5C Internal";
    case TEMP_SRC_NONE:
    default:
        return "No Sensor";
    }
}
