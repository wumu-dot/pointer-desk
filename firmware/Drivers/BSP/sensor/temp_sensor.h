/**
 * temp_sensor.h — 温度传感器抽象接口
 *
 * 支持两个实现:
 *   1. temp_adc.c  — 内部 ADC 温度传感器 (±5°C, 初期方案)
 *   2. sht30.c     — 外部 SHT30 I2C 传感器 (±0.3°C, 扩展方案)
 *
 * 通过此统一接口调用，上层代码无需关心具体实现。
 */

#ifndef TEMP_SENSOR_H
#define TEMP_SENSOR_H

#include <stdint.h>
#include <stdbool.h>

/* ================================================================
 * 传感器类型
 * ================================================================ */
typedef enum {
    TEMP_SRC_INTERNAL_ADC,  /* STM32 内部 ADC */
    TEMP_SRC_SHT30,         /* 外部 SHT30 */
    TEMP_SRC_NONE,          /* 无传感器 */
} temp_source_t;

/* ================================================================
 * 传感器数据
 * ================================================================ */
typedef struct {
    float          temperature;   /* 温度 °C */
    float          humidity;      /* 湿度 % (SHT30 可用，ADC 返回 0) */
    temp_source_t  source;        /* 数据来源 */
    uint32_t       timestamp_ms;  /* 采样时间 */
} temp_data_t;

/* ================================================================
 * API
 * ================================================================ */
void temp_sensor_init(void);

/* 读取温度 (自动选择最佳可用传感器) */
temp_data_t temp_sensor_read(void);

/* 检查传感器状态 */
bool temp_sensor_has_external(void);  /* SHT30 是否插入 */
temp_source_t temp_sensor_get_source(void);

/* 精度描述字符串 ("±5°C 内部" / "±0.3°C SHT30") */
const char* temp_sensor_get_label(void);

/* ================================================================
 * 具体实现接口 (temp_adc.c / sht30.c 各自实现)
 * ================================================================ */
void temp_adc_init(void);
float temp_adc_read_celsius(void);

void sht30_init(void);
bool sht30_is_present(void);
temp_data_t sht30_read(void);

#endif /* TEMP_SENSOR_H */
