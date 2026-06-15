/**
 * pin_config.h — 引脚映射定义
 *
 * 所有硬件引脚统一在此定义，方便修改移植。
 * 对应 CubeMX 中的 GPIO/Pinout 配置。
 */

#ifndef PIN_CONFIG_H
#define PIN_CONFIG_H

/* ================================================================
 * SPI1 — TFT LCD (ST7735S)
 * ================================================================ */
#define LCD_SPI             SPI1
#define LCD_SPI_CLK_ENABLE  __HAL_RCC_SPI1_CLK_ENABLE

#define LCD_SCK_PIN         GPIO_PIN_5
#define LCD_SCK_PORT        GPIOA
#define LCD_MOSI_PIN        GPIO_PIN_7
#define LCD_MOSI_PORT       GPIOA
#define LCD_CS_PIN          GPIO_PIN_4
#define LCD_CS_PORT         GPIOA
#define LCD_DC_PIN          GPIO_PIN_4
#define LCD_DC_PORT         GPIOC
#define LCD_RST_PIN         GPIO_PIN_5
#define LCD_RST_PORT        GPIOC
#define LCD_BL_PIN          GPIO_PIN_0
#define LCD_BL_PORT         GPIOB

/* TFT 参数 */
#define LCD_WIDTH           128
#define LCD_HEIGHT          160
#define LCD_SPI_BAUDRATE    9000000UL   /* 9MHz, 面包板稳定速率 */
#define LCD_ROTATION        0           /* 0/1/2/3 旋转角度 */

/* 背光 PWM */
#define LCD_BL_TIM          TIM3
#define LCD_BL_CHANNEL      TIM_CHANNEL_3

/* ================================================================
 * SPI2 — Flash (W25Q64)
 * ================================================================ */
#define FLASH_SPI           SPI2
#define FLASH_SPI_CLK_ENABLE __HAL_RCC_SPI2_CLK_ENABLE

#define FLASH_SCK_PIN       GPIO_PIN_13
#define FLASH_SCK_PORT      GPIOB
#define FLASH_MOSI_PIN      GPIO_PIN_15
#define FLASH_MOSI_PORT     GPIOB
#define FLASH_MISO_PIN      GPIO_PIN_14
#define FLASH_MISO_PORT     GPIOB
#define FLASH_CS_PIN        GPIO_PIN_12
#define FLASH_CS_PORT       GPIOB

/* W25Q64 参数 */
#define FLASH_SIZE_BYTES    (8 * 1024 * 1024)  /* 8MB */
#define FLASH_SECTOR_SIZE   4096
#define FLASH_PAGE_SIZE     256
#define FLASH_SPI_BAUDRATE  10000000UL

/* LittleFS 分区 */
#define LFS_CONFIG_OFFSET   0x000000UL         /* 1MB 配置区 */
#define LFS_CONFIG_SIZE     (1 * 1024 * 1024)
#define LFS_FONT_OFFSET     0x100000UL         /* 3MB 字体区 */
#define LFS_FONT_SIZE       (3 * 1024 * 1024)
#define LFS_IMAGE_OFFSET    0x400000UL         /* 4MB 图片区 */
#define LFS_IMAGE_SIZE      (4 * 1024 * 1024)

/* ================================================================
 * 步进电机 (A4988)
 * ================================================================ */
#define MOTOR_STEP_PIN      GPIO_PIN_0
#define MOTOR_STEP_PORT     GPIOA
#define MOTOR_DIR_PIN       GPIO_PIN_1
#define MOTOR_DIR_PORT      GPIOA
#define MOTOR_EN_PIN        GPIO_PIN_2
#define MOTOR_EN_PORT       GPIOA

/* MS1/MS2/MS3 细分设置 (16细分: 全高) */
#define MOTOR_MS1_PIN       GPIO_PIN_3
#define MOTOR_MS1_PORT      GPIOA
#define MOTOR_MS2_PIN       GPIO_PIN_3    /* 同引脚，跳线连一起 */
#define MOTOR_MS2_PORT      GPIOA
#define MOTOR_MS3_PIN       GPIO_PIN_3
#define MOTOR_MS3_PORT      GPIOA

/* STEP 脉冲 PWM */
#define MOTOR_STEP_TIM      TIM2
#define MOTOR_STEP_CHANNEL  TIM_CHANNEL_1

/* 电机参数 */
#define MOTOR_STEPS_PER_REV 200            /* 整步/圈 */
#define MOTOR_MICROSTEPS    16             /* 16细分 */
#define MOTOR_TOTAL_STEPS   (MOTOR_STEPS_PER_REV * MOTOR_MICROSTEPS) /* 3200 */
#define MOTOR_MAX_SPEED     500            /* 最大速度 (步/秒) */
#define MOTOR_ACCEL         800            /* 加速度 (步/秒²) */

/* ================================================================
 * 按键 — PA15 (KEY) 单键输入
 * ================================================================ */
#define BTN_CENTER_PIN      GPIO_PIN_15
#define BTN_CENTER_PORT     GPIOA
/* 其他按键未连接, 指向同引脚避免扫描异常 */
#define BTN_UP_PIN          GPIO_PIN_15
#define BTN_UP_PORT         GPIOA
#define BTN_DOWN_PIN        GPIO_PIN_15
#define BTN_DOWN_PORT       GPIOA
#define BTN_LEFT_PIN        GPIO_PIN_15
#define BTN_LEFT_PORT       GPIOA
#define BTN_RIGHT_PIN       GPIO_PIN_15
#define BTN_RIGHT_PORT      GPIOA

/* 按键参数 */
#define BTN_DEBOUNCE_MS     20          /* 去抖时间 */
#define BTN_LONG_PRESS_MS   500         /* 长按判定时间 */
#define BTN_POLL_MS         10          /* 轮询周期 */

/* ================================================================
 * 温度传感器
 * ================================================================ */
/* 内部 ADC */
#define TEMP_ADC            ADC1
#define TEMP_ADC_CHANNEL    ADC_CHANNEL_TEMPSENSOR

/* 外部 SHT30 (预留) */
#define SHT30_I2C           I2C2
#define SHT30_I2C_SCL_PIN   GPIO_PIN_10
#define SHT30_I2C_SCL_PORT  GPIOB
#define SHT30_I2C_SDA_PIN   GPIO_PIN_11
#define SHT30_I2C_SDA_PORT  GPIOB
#define SHT30_I2C_ADDR      0x44

/* ================================================================
 * RTC
 * ================================================================ */
#define RTC_LSE_CLK         32768UL

/* ================================================================
 * 系统 & 调试
 * ================================================================ */
/* 系统时钟 */
#define SYS_CLK_FREQ        168000000UL
#define APB1_TIM_CLK        84000000UL
#define APB2_TIM_CLK        84000000UL

/* 串口调试 */
#define DEBUG_UART          USART1
#define DEBUG_UART_BAUD     115200
#define DEBUG_TX_PIN        GPIO_PIN_9
#define DEBUG_TX_PORT       GPIOA
#define DEBUG_RX_PIN        GPIO_PIN_10
#define DEBUG_RX_PORT       GPIOA

#endif /* PIN_CONFIG_H */
