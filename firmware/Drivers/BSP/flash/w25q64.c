/**
 * w25q64.c — W25Q64 8MB SPI Flash 驱动实现
 *
 * 基于 STM32F4 HAL 库，使用 SPI2 (PB12=CS, PB13=SCK, PB14=MISO, PB15=MOSI)
 * 支持全功能读写擦除操作，所有写/擦除操作自动等待就绪。
 */

#include "w25q64.h"
#include "main.h"
#include "pin_config.h"
#include "app_config.h"

/* ================================================================
 * 私有宏定义
 * ================================================================ */

/* W25Q64 指令集 */
#define W25Q_CMD_WRITE_ENABLE       0x06
#define W25Q_CMD_WRITE_DISABLE      0x04
#define W25Q_CMD_READ_STATUS1       0x05
#define W25Q_CMD_READ_STATUS2       0x35
#define W25Q_CMD_READ_DATA          0x03
#define W25Q_CMD_PAGE_PROGRAM       0x02
#define W25Q_CMD_SECTOR_ERASE       0x20
#define W25Q_CMD_BLOCK_ERASE_32K    0x52
#define W25Q_CMD_BLOCK_ERASE_64K    0xD8
#define W25Q_CMD_CHIP_ERASE         0xC7
#define W25Q_CMD_POWER_DOWN         0xB9
#define W25Q_CMD_RELEASE_PD         0xAB
#define W25Q_CMD_JEDEC_ID           0x9F
#define W25Q_CMD_UNIQUE_ID          0x4B

/* Status Register 1 位定义 */
#define W25Q_SR1_BUSY               (1 << 0)   /* BUSY: 擦除/编程进行中 */
#define W25Q_SR1_WEL                (1 << 1)   /* WEL: 写使能锁存 */

/* 容量常量 */
#define W25Q_CAPACITY_BYTES         (8UL * 1024UL * 1024UL)
#define W25Q_SECTOR_SIZE            4096UL
#define W25Q_PAGE_SIZE              256UL

/* SPI 超时 */
#define W25Q_SPI_TIMEOUT            1000U      /* HAL 传输超时 (ms) */
#define W25Q_CHIP_ERASE_TIMEOUT     120000U    /* 整片擦除超时 (ms) */
#define W25Q_SECTOR_ERASE_TIMEOUT   400U       /* 扇区擦除超时 (ms) */
#define W25Q_BLOCK_ERASE_TIMEOUT    2000U      /* 块擦除超时 (ms) */
#define W25Q_PAGE_WRITE_TIMEOUT     10U        /* 页编程超时 (ms) */

/* ================================================================
 * 外部句柄声明
 * ================================================================ */

extern SPI_HandleTypeDef hspi2;

/* ================================================================
 * 私有辅助函数 (前向声明)
 * ================================================================ */

static void w25q64_cs_low(void);
static void w25q64_cs_high(void);
static void w25q64_write_enable(void);
static uint8_t w25q64_read_status1(void);
/* static uint8_t w25q64_read_status2(void); — currently unused */
static void w25q64_spi_transmit(const uint8_t *data, uint16_t len);
static void w25q64_spi_receive(uint8_t *data, uint16_t len);
static void w25q64_send_address(uint32_t addr);
static void w25q64_wait_ready_timeout(uint32_t timeout_ms);

/* ================================================================
 * CS 引脚控制
 * ================================================================ */

static void w25q64_cs_low(void)
{
    HAL_GPIO_WritePin(FLASH_CS_PORT, FLASH_CS_PIN, GPIO_PIN_RESET);
}

static void w25q64_cs_high(void)
{
    HAL_GPIO_WritePin(FLASH_CS_PORT, FLASH_CS_PIN, GPIO_PIN_SET);
}

/* ================================================================
 * SPI 底层传输
 * ================================================================ */

static void w25q64_spi_transmit(const uint8_t *data, uint16_t len)
{
    HAL_StatusTypeDef status;
    status = HAL_SPI_Transmit(&hspi2, (uint8_t *)data, len, W25Q_SPI_TIMEOUT);
    if (status != HAL_OK) {
        LOG_ERR("SPI transmit failed: %d", status);
    }
}

static void w25q64_spi_receive(uint8_t *data, uint16_t len)
{
    HAL_StatusTypeDef status;
    status = HAL_SPI_Receive(&hspi2, data, len, W25Q_SPI_TIMEOUT);
    if (status != HAL_OK) {
        LOG_ERR("SPI receive failed: %d", status);
    }
}

/* ================================================================
 * 发送 24 位地址 (MSB first)
 * ================================================================ */

static void w25q64_send_address(uint32_t addr)
{
    uint8_t addr_bytes[3];
    addr_bytes[0] = (uint8_t)((addr >> 16) & 0xFF);
    addr_bytes[1] = (uint8_t)((addr >> 8) & 0xFF);
    addr_bytes[2] = (uint8_t)(addr & 0xFF);
    w25q64_spi_transmit(addr_bytes, 3);
}

/* ================================================================
 * 写使能
 * ================================================================ */

static void w25q64_write_enable(void)
{
    uint8_t cmd = W25Q_CMD_WRITE_ENABLE;
    w25q64_cs_low();
    w25q64_spi_transmit(&cmd, 1);
    w25q64_cs_high();

    /* 验证 WEL 位 */
    uint32_t timeout = 1000;
    while (timeout--) {
        if (w25q64_read_status1() & W25Q_SR1_WEL) {
            return;
        }
    }
    LOG_ERR("Write enable failed: WEL not set");
}

/* ================================================================
 * 写禁止
 * ================================================================ */

static void w25q64_write_disable(void)
{
    uint8_t cmd = W25Q_CMD_WRITE_DISABLE;
    w25q64_cs_low();
    w25q64_spi_transmit(&cmd, 1);
    w25q64_cs_high();
}

/* ================================================================
 * 读取状态寄存器
 * ================================================================ */

static uint8_t w25q64_read_status1(void)
{
    uint8_t cmd = W25Q_CMD_READ_STATUS1;
    uint8_t status = 0;
    w25q64_cs_low();
    w25q64_spi_transmit(&cmd, 1);
    w25q64_spi_receive(&status, 1);
    w25q64_cs_high();
    return status;
}

/*
static uint8_t w25q64_read_status2(void)
{
    uint8_t cmd = W25Q_CMD_READ_STATUS2;
    uint8_t status = 0;
    w25q64_cs_low();
    w25q64_spi_transmit(&cmd, 1);
    w25q64_spi_receive(&status, 1);
    w25q64_cs_high();
    return status;
}
*/

/* ================================================================
 * 等待就绪 (带超时)
 * ================================================================ */

static void w25q64_wait_ready_timeout(uint32_t timeout_ms)
{
    uint32_t elapsed = 0;
    while (w25q64_is_busy()) {
        if (elapsed >= timeout_ms) {
            LOG_ERR("Flash operation timed out after %lu ms", timeout_ms);
            return;
        }
        HAL_Delay(1);
        elapsed++;
    }
}

/* ================================================================
 * 公开接口: 初始化 & 信息
 * ================================================================ */

/**
 * @brief 初始化 W25Q64
 * @note  SPI2 外设在 main.c 的 MX_SPI2_Init() 中已初始化，
 *        此函数配置 CS 引脚并确保芯片处于可操作状态。
 */
bool w25q64_init(void)
{
    LOG("W25Q64 init start");

    /* 初始化 CS 引脚为高电平 (不选中) */
    GPIO_InitTypeDef gpio_init = {0};
    gpio_init.Pin = FLASH_CS_PIN;
    gpio_init.Mode = GPIO_MODE_OUTPUT_PP;
    gpio_init.Pull = GPIO_PULLUP;
    gpio_init.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(FLASH_CS_PORT, &gpio_init);
    w25q64_cs_high();

    /*
     * 硬件复位: 发送 "Enable Reset (0x66)" + "Reset Device (0x99)"
     */
    {
        uint8_t reset_seq[] = {0x66, 0x99};
        w25q64_cs_low();
        w25q64_spi_transmit(reset_seq, 2);
        w25q64_cs_high();
        HAL_Delay(30);
    }

    /* 唤醒芯片 + 写保护关 */
    w25q64_wake_up();
    HAL_Delay(1);
    w25q64_write_disable();

    /* 读取 JEDEC ID (W25Q64 = 0xEF4017) */
    uint32_t jedec_id = w25q64_get_id();
    if (jedec_id == 0x00000000 || jedec_id == 0xFFFFFFFF || jedec_id == 0x00FFFFFF) {
        LOG("W25Q64 NOT FOUND — JEDEC=0x%06lX, skipping flash ops", jedec_id);
        return false;
    }

    LOG("W25Q64 init done. JEDEC ID: 0x%06lX", jedec_id);
    return true;
}

/**
 * @brief 读取 JEDEC 制造商/设备 ID
 * @return 24位 JEDEC ID (W25Q64 应为 0xEF4017)
 */
uint32_t w25q64_get_id(void)
{
    uint8_t cmd = W25Q_CMD_JEDEC_ID;
    uint8_t id[3] = {0};

    w25q64_cs_low();
    w25q64_spi_transmit(&cmd, 1);
    w25q64_spi_receive(id, 3);
    w25q64_cs_high();

    return ((uint32_t)id[0] << 16) | ((uint32_t)id[1] << 8) | (uint32_t)id[2];
}

/**
 * @brief 读取 64 位唯一序列号
 * @return 64 位 Unique ID
 */
uint64_t w25q64_get_unique_id(void)
{
    uint8_t cmd = W25Q_CMD_UNIQUE_ID;
    uint8_t uid[8] = {0};
    uint64_t unique_id = 0;

    w25q64_cs_low();
    w25q64_spi_transmit(&cmd, 1);
    /* Unique ID 命令后需发 4 字节 dummy 再读取 8 字节 */
    uint8_t dummy[4] = {0xFF, 0xFF, 0xFF, 0xFF};
    w25q64_spi_transmit(dummy, 4);
    w25q64_spi_receive(uid, 8);
    w25q64_cs_high();

    for (int i = 0; i < 8; i++) {
        unique_id = (unique_id << 8) | uid[i];
    }
    return unique_id;
}

/**
 * @brief 获取 Flash 容量 (字节)
 * @return 8388608 (8MB)
 */
uint32_t w25q64_get_capacity(void)
{
    return W25Q_CAPACITY_BYTES;
}

/* ================================================================
 * 公开接口: 擦除
 * ================================================================ */

/**
 * @brief 整片擦除 (耗时约 40~80 秒)
 * @note  仅应在初始化或格式化时调用，会阻塞较长时间
 */
void w25q64_erase_chip(void)
{
    LOG("Chip erase started (may take ~60s)...");

    w25q64_write_enable();

    uint8_t cmd = W25Q_CMD_CHIP_ERASE;
    w25q64_cs_low();
    w25q64_spi_transmit(&cmd, 1);
    w25q64_cs_high();

    w25q64_wait_ready_timeout(W25Q_CHIP_ERASE_TIMEOUT);

    LOG("Chip erase done");
}

/**
 * @brief 扇区擦除 (4KB)
 * @param addr 扇区起始地址 (自动对齐到扇区边界)
 */
void w25q64_erase_sector(uint32_t addr)
{
    w25q64_write_enable();

    uint8_t cmd = W25Q_CMD_SECTOR_ERASE;
    w25q64_cs_low();
    w25q64_spi_transmit(&cmd, 1);
    w25q64_send_address(addr);
    w25q64_cs_high();

    w25q64_wait_ready_timeout(W25Q_SECTOR_ERASE_TIMEOUT);
}

/**
 * @brief 32KB 块擦除
 * @param addr 块起始地址
 */
void w25q64_erase_block_32k(uint32_t addr)
{
    w25q64_write_enable();

    uint8_t cmd = W25Q_CMD_BLOCK_ERASE_32K;
    w25q64_cs_low();
    w25q64_spi_transmit(&cmd, 1);
    w25q64_send_address(addr);
    w25q64_cs_high();

    w25q64_wait_ready_timeout(W25Q_BLOCK_ERASE_TIMEOUT);
}

/**
 * @brief 64KB 块擦除
 * @param addr 块起始地址
 */
void w25q64_erase_block_64k(uint32_t addr)
{
    w25q64_write_enable();

    uint8_t cmd = W25Q_CMD_BLOCK_ERASE_64K;
    w25q64_cs_low();
    w25q64_spi_transmit(&cmd, 1);
    w25q64_send_address(addr);
    w25q64_cs_high();

    w25q64_wait_ready_timeout(W25Q_BLOCK_ERASE_TIMEOUT);
}

/* ================================================================
 * 公开接口: 读
 * ================================================================ */

/**
 * @brief 从 Flash 读取数据
 * @param addr 起始地址 (24位)
 * @param buf  输出缓冲区
 * @param len  读取字节数
 * @note  支持跨页连续读取，自动处理地址递增
 */
void w25q64_read(uint32_t addr, uint8_t *buf, uint32_t len)
{
    if (buf == NULL || len == 0) {
        return;
    }

    /* 地址边界检查 */
    if (addr >= W25Q_CAPACITY_BYTES) {
        LOG_ERR("Read address out of range: 0x%08lX", addr);
        return;
    }

    /* 截断到容量边界 */
    if ((uint64_t)addr + len > W25Q_CAPACITY_BYTES) {
        len = W25Q_CAPACITY_BYTES - addr;
    }

    uint8_t cmd = W25Q_CMD_READ_DATA;
    w25q64_cs_low();
    w25q64_spi_transmit(&cmd, 1);
    w25q64_send_address(addr);
    w25q64_spi_receive(buf, (uint16_t)len);
    w25q64_cs_high();
}

/* ================================================================
 * 公开接口: 写 (页编程 & 自动跨页)
 * ================================================================ */

/**
 * @brief 单页写入 (不超过 256 字节)
 * @param addr 写入起始地址
 * @param buf  数据缓冲区
 * @param len  写入字节数 (调用者需保证 ≤256 且不跨页)
 */
void w25q64_write_page(uint32_t addr, const uint8_t *buf, uint32_t len)
{
    if (buf == NULL || len == 0) {
        return;
    }

    if (len > W25Q_PAGE_SIZE) {
        LOG_ERR("Page write exceeds 256 bytes: %lu", len);
        return;
    }

    w25q64_write_enable();

    uint8_t cmd = W25Q_CMD_PAGE_PROGRAM;
    w25q64_cs_low();
    w25q64_spi_transmit(&cmd, 1);
    w25q64_send_address(addr);
    w25q64_spi_transmit(buf, (uint16_t)len);
    w25q64_cs_high();

    w25q64_wait_ready_timeout(W25Q_PAGE_WRITE_TIMEOUT);
}

/**
 * @brief 多页写入 (自动跨页拆分)
 * @param addr 写入起始地址
 * @param buf  数据缓冲区
 * @param len  写入字节数
 * @note  自动将超过 256 字节或跨页的写入拆分为多次页编程
 */
void w25q64_write(uint32_t addr, const uint8_t *buf, uint32_t len)
{
    if (buf == NULL || len == 0) {
        return;
    }

    /* 地址边界检查 */
    if (addr >= W25Q_CAPACITY_BYTES) {
        LOG_ERR("Write address out of range: 0x%08lX", addr);
        return;
    }

    uint32_t remaining = len;
    uint32_t current_addr = addr;
    const uint8_t *src = buf;

    while (remaining > 0) {
        /* 计算当前页剩余空间 */
        uint32_t page_offset = current_addr % W25Q_PAGE_SIZE;
        uint32_t space_in_page = W25Q_PAGE_SIZE - page_offset;
        uint32_t chunk = (remaining < space_in_page) ? remaining : space_in_page;

        /* 容量边界截断 */
        if ((uint64_t)current_addr + chunk > W25Q_CAPACITY_BYTES) {
            chunk = W25Q_CAPACITY_BYTES - current_addr;
            if (chunk == 0) break;
        }

        w25q64_write_page(current_addr, src, chunk);

        current_addr += chunk;
        src += chunk;
        remaining -= chunk;
    }
}

/* ================================================================
 * 公开接口: 状态 & 电源管理
 * ================================================================ */

/**
 * @brief 检查芯片是否忙 (擦除/编程进行中)
 * @return true=忙, false=就绪
 */
bool w25q64_is_busy(void)
{
    return (w25q64_read_status1() & W25Q_SR1_BUSY) != 0;
}

/**
 * @brief 阻塞等待芯片就绪 (无超时)
 */
void w25q64_wait_ready(void)
{
    while (w25q64_is_busy()) {
        /* 忙等待 */
    }
}

/**
 * @brief 进入掉电模式 (功耗最低)
 * @note  唤醒需调用 w25q64_wake_up()
 */
void w25q64_power_down(void)
{
    uint8_t cmd = W25Q_CMD_POWER_DOWN;
    w25q64_cs_low();
    w25q64_spi_transmit(&cmd, 1);
    w25q64_cs_high();

    LOG("W25Q64 power down");
}

/**
 * @brief 从掉电模式唤醒
 * @note  需等待 tRES1 (~3µs) 后芯片才可用
 */
void w25q64_wake_up(void)
{
    uint8_t cmd = W25Q_CMD_RELEASE_PD;
    w25q64_cs_low();
    w25q64_spi_transmit(&cmd, 1);
    w25q64_cs_high();

    /* 等待唤醒时间 (tRES1 ≤ 3µs, 这里给足够的余量) */
    HAL_Delay(1);
}
