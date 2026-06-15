/**
 * w25q64.h — W25Q64 SPI Flash 驱动接口
 */

#ifndef W25Q64_H
#define W25Q64_H

#include <stdint.h>
#include <stdbool.h>

/* ================================================================
 * 初始化 & 信息
 * ================================================================ */
bool w25q64_init(void);        /* 返回 true=芯片存在, false=未检测到 */
uint32_t w25q64_get_id(void);
uint64_t w25q64_get_unique_id(void);
uint32_t w25q64_get_capacity(void);

/* ================================================================
 * 擦除
 * ================================================================ */
void w25q64_erase_chip(void);               /* 整片擦除 (耗时!) */
void w25q64_erase_sector(uint32_t addr);    /* 4KB 扇区 */
void w25q64_erase_block_32k(uint32_t addr); /* 32KB 块 */
void w25q64_erase_block_64k(uint32_t addr); /* 64KB 块 */

/* ================================================================
 * 读写
 * ================================================================ */
void w25q64_read(uint32_t addr, uint8_t *buf, uint32_t len);
void w25q64_write_page(uint32_t addr, const uint8_t *buf, uint32_t len); /* ≤256B */
void w25q64_write(uint32_t addr, const uint8_t *buf, uint32_t len);      /* 自动跨页 */

/* ================================================================
 * 状态
 * ================================================================ */
bool w25q64_is_busy(void);
void w25q64_wait_ready(void);
void w25q64_power_down(void);
void w25q64_wake_up(void);

#endif /* W25Q64_H */
