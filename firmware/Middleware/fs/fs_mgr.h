/**
 * fs_mgr.h — LittleFS 文件系统管理
 *
 * 封装 W25Q64 上的 LittleFS 操作。
 * 分为两个区：
 *   - 配置区 (1MB): 系统设置，恢复出厂时擦除
 *   - 资源区 (7MB): 字体/图片，只读
 */

#ifndef FS_MGR_H
#define FS_MGR_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

/* ================================================================
 * 初始化
 * ================================================================ */
void fs_mgr_init(void);
bool fs_mgr_is_mounted(void);

/* ================================================================
 * 配置区操作 (1MB, 可读写)
 * ================================================================ */
bool fs_config_save(const char *key, const void *data, size_t size);
bool fs_config_load(const char *key, void *data, size_t size);
bool fs_config_exists(const char *key);
bool fs_config_delete(const char *key);
void fs_config_format(void);        /* 格式化配置区 (恢复出厂) */

/* ================================================================
 * 资源区操作 (7MB, 预写入只读)
 * ================================================================ */
void* fs_res_open(const char *path);                    /* 返回文件数据指针 */
size_t fs_res_size(const char *path);
bool fs_res_exists(const char *path);

/* ================================================================
 * 通用文件操作 (调试用)
 * ================================================================ */
bool fs_file_read(const char *path, void *buf, size_t size);
bool fs_file_write(const char *path, const void *buf, size_t size);
bool fs_file_remove(const char *path);

/* ================================================================
 * 空间查询
 * ================================================================ */
uint32_t fs_get_free_config(void);
uint32_t fs_get_total_config(void);

#endif /* FS_MGR_H */
