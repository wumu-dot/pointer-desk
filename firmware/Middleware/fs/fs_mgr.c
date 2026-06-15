/**
 * fs_mgr.c — LittleFS 文件系统管理 (W25Q64 Flash 模拟层)
 *
 * 采用扁平目录方案在 W25Q64 上实现简单的 KV 存储。
 * - Sector 0: 目录表 (头 + 32 条目录项)
 * - Sector 1~255: 数据区 (文件内容连续存放)
 *
 * 依赖: w25q64.c (Flash 驱动), pin_config.h (分区偏移), app_config.h (LOG 宏)
 */

#include "fs_mgr.h"
#include "w25q64.h"
#include "pin_config.h"
#include "app_config.h"
#include <string.h>

/* ================================================================
 * 私有常量
 * ================================================================ */

#define FS_MAGIC                0x4F564653UL    /* "OVFS" */
#define FS_VERSION              1
#define FS_MAX_ENTRIES          32
#define FS_ENTRY_SIZE           sizeof(fs_entry_t)

#define CONFIG_DIR_SECTOR       LFS_CONFIG_OFFSET           /* 目录扇区起始 */
#define CONFIG_DIR_SIZE         FLASH_SECTOR_SIZE           /* 目录占用 1 个扇区 */
#define CONFIG_DATA_START       (LFS_CONFIG_OFFSET + FLASH_SECTOR_SIZE)  /* 数据起始 */
#define CONFIG_DATA_END         (LFS_CONFIG_OFFSET + LFS_CONFIG_SIZE)    /* 数据结束 */

/* 条目标志位 */
#define FS_FLAG_VALID           0x01    /* 条目有效 */
#define FS_FLAG_READONLY        0x02    /* 只读 */

/* ================================================================
 * 目录数据结构 (packed, 与 Flash 布局一致)
 * ================================================================ */

typedef struct __attribute__((packed)) {
    char     key[32];            /* 文件名 / 配置键 */
    uint32_t offset;             /* 数据区字节偏移 */
    uint32_t size;               /* 文件大小 (字节) */
    uint8_t  flags;              /* 位0=有效, 位1=只读 */
    uint8_t  reserved[3];        /* 对齐填充 */
} fs_entry_t;

typedef struct __attribute__((packed)) {
    uint32_t    magic;           /* FS_MAGIC */
    uint32_t    version;         /* 格式版本 */
    uint8_t     entry_count;     /* 当前有效条目数 */
    uint8_t     reserved[3];     /* 对齐填充 */
    fs_entry_t  entries[FS_MAX_ENTRIES];
} fs_dir_t;

/* ================================================================
 * 静态状态
 * ================================================================ */

static fs_dir_t  g_dir;                  /* 目录内存缓存 */
static uint32_t  g_next_data_offset = 0; /* 数据区下一个空闲偏移 */
static bool      g_mounted = false;      /* 挂载状态 */

/* ================================================================
 * 私有辅助: 按 key 查找条目, 返回索引或 -1
 * ================================================================ */

static int find_entry(const char *key)
{
    if (key == NULL) return -1;

    for (int i = 0; i < FS_MAX_ENTRIES; i++) {
        if ((g_dir.entries[i].flags & FS_FLAG_VALID) &&
            strncmp(g_dir.entries[i].key, key, sizeof(g_dir.entries[i].key)) == 0) {
            return i;
        }
    }
    return -1;
}

/* ================================================================
 * 私有辅助: 分配新条目 (返回第一个空闲槽位索引, -1 表示满)
 * ================================================================ */

static int alloc_entry(void)
{
    for (int i = 0; i < FS_MAX_ENTRIES; i++) {
        if (!(g_dir.entries[i].flags & FS_FLAG_VALID)) {
            return i;
        }
    }
    return -1;
}

/* ================================================================
 * 私有辅助: 将目录缓存刷写到 Flash Sector 0
 * ================================================================ */

static bool flush_directory(void)
{
    /* 擦除目录扇区 */
    w25q64_erase_sector(CONFIG_DIR_SECTOR);

    /* 写回整个目录结构 (w25q64_write 自动处理跨页) */
    w25q64_write(CONFIG_DIR_SECTOR, (const uint8_t *)&g_dir, sizeof(g_dir));

    LOG("Directory flushed (%u entries)", g_dir.entry_count);
    return true;
}

/* ================================================================
 * 私有辅助: 格式化配置区 (内部使用, 不检查 g_mounted)
 * ================================================================ */

static void format_internal(void)
{
    LOG("Formatting config partition...");

    /* 仅擦除目录扇区 (Sector 0)，数据扇区按需写入时自动处理 */
    w25q64_erase_sector(CONFIG_DIR_SECTOR);

    /* 初始化内存中的目录结构 */
    memset(&g_dir, 0, sizeof(g_dir));
    g_dir.magic   = FS_MAGIC;
    g_dir.version = FS_VERSION;
    g_dir.entry_count = 0;

    /* 写目录到 Sector 0 */
    flush_directory();

    /* 重置数据区偏移 */
    g_next_data_offset = CONFIG_DATA_START;

    LOG("Format complete. Data area: %lu bytes free",
        (uint32_t)(CONFIG_DATA_END - CONFIG_DATA_START));
}

/* ================================================================
 * 公开接口: 初始化
 * ================================================================ */

/**
 * @brief 初始化文件系统
 * @note  必须先调用 w25q64_init() (在 main.c 中由系统初始化),
 *        此函数从 Flash 读取目录并验证魔数, 无效则格式化。
 */
void fs_mgr_init(void)
{
    /* 如果 Flash 芯片未检测到, 跳过文件系统 */
    if (!w25q64_init()) {
        g_mounted = false;
        LOG("fs_mgr: Flash absent, filesystem disabled");
        return;
    }

    LOG("fs_mgr init start");

    /* 读取目录扇区 */
    w25q64_read(CONFIG_DIR_SECTOR, (uint8_t *)&g_dir, sizeof(g_dir));

    /* 验证魔数 */
    if (g_dir.magic != FS_MAGIC) {
        LOG("Bad magic 0x%08lX, expected 0x%08lX -- formatting",
            (unsigned long)g_dir.magic, (unsigned long)FS_MAGIC);
        format_internal();
    } else {
        LOG("fs_mgr mounted OK, version=%lu, %u entries",
            (unsigned long)g_dir.version, g_dir.entry_count);
    }

    /* 计算下一个可用数据偏移 */
    g_next_data_offset = CONFIG_DATA_START;
    for (int i = 0; i < FS_MAX_ENTRIES; i++) {
        if (g_dir.entries[i].flags & FS_FLAG_VALID) {
            uint32_t end = g_dir.entries[i].offset + g_dir.entries[i].size;
            if (end > g_next_data_offset) {
                g_next_data_offset = end;
            }
        }
    }

    g_mounted = true;
    LOG("fs_mgr init done, next_data=0x%08lX", (unsigned long)g_next_data_offset);
}

/* ================================================================
 * 公开接口: 挂载状态
 * ================================================================ */

bool fs_mgr_is_mounted(void)
{
    return g_mounted;
}

/* ================================================================
 * 公开接口: 配置区 — 保存
 * ================================================================ */

bool fs_config_save(const char *key, const void *data, size_t size)
{
    if (!g_mounted || key == NULL || data == NULL || size == 0) {
        LOG_ERR("fs_config_save: invalid args");
        return false;
    }

    if (strlen(key) >= 32) {
        LOG_ERR("fs_config_save: key too long (max 31 chars)");
        return false;
    }

    /* 检查数据区是否有足够空间 */
    if ((uint32_t)(g_next_data_offset + size) > CONFIG_DATA_END) {
        LOG_ERR("fs_config_save: out of space (need %u + %u > %lu)",
                (unsigned int)g_next_data_offset, (unsigned int)size,
                (unsigned long)CONFIG_DATA_END);
        return false;
    }

    int idx = find_entry(key);

    /* 如果是新条目, 分配槽位 */
    if (idx < 0) {
        idx = alloc_entry();
        if (idx < 0) {
            LOG_ERR("fs_config_save: directory full (%d entries)", FS_MAX_ENTRIES);
            return false;
        }
    }

    /* 写入数据到 Flash (到当前空闲位置; 旧数据原地废弃) */
    uint32_t write_offset = g_next_data_offset;
    w25q64_write(write_offset, (const uint8_t *)data, (uint32_t)size);

    /* 更新目录条目 */
    bool is_new = (!(g_dir.entries[idx].flags & FS_FLAG_VALID));
    memset(&g_dir.entries[idx], 0, sizeof(fs_entry_t));
    strncpy(g_dir.entries[idx].key, key, sizeof(g_dir.entries[idx].key) - 1);
    g_dir.entries[idx].key[sizeof(g_dir.entries[idx].key) - 1] = '\0';
    g_dir.entries[idx].offset = write_offset;
    g_dir.entries[idx].size   = (uint32_t)size;
    g_dir.entries[idx].flags  = FS_FLAG_VALID;

    /* 新增条目时重新统计 (覆盖已有条目则计数不变) */
    if (is_new) {
        uint8_t count = 0;
        for (int i = 0; i < FS_MAX_ENTRIES; i++) {
            if (g_dir.entries[i].flags & FS_FLAG_VALID) count++;
        }
        g_dir.entry_count = count;
    }

    /* 刷写目录到 Flash */
    if (!flush_directory()) {
        LOG_ERR("fs_config_save: flush failed");
        return false;
    }

    /* 推进空闲偏移 */
    g_next_data_offset = write_offset + (uint32_t)size;

    LOG("fs_config_save '%s': %u bytes @ 0x%08lX", key,
        (unsigned int)size, (unsigned long)write_offset);
    return true;
}

/* ================================================================
 * 公开接口: 配置区 — 加载
 * ================================================================ */

bool fs_config_load(const char *key, void *data, size_t size)
{
    if (!g_mounted || key == NULL || data == NULL || size == 0) {
        LOG_ERR("fs_config_load: invalid args");
        return false;
    }

    int idx = find_entry(key);
    if (idx < 0) {
        LOG_ERR("fs_config_load: key '%s' not found", key);
        return false;
    }

    fs_entry_t *entry = &g_dir.entries[idx];

    /* 读取数据 (取 min(size, entry->size)) */
    uint32_t read_size = (size < entry->size) ? (uint32_t)size : entry->size;
    w25q64_read(entry->offset, (uint8_t *)data, read_size);

    LOG("fs_config_load '%s': %u bytes from 0x%08lX", key,
        (unsigned int)read_size, (unsigned long)entry->offset);
    return true;
}

/* ================================================================
 * 公开接口: 配置区 — 检测存在
 * ================================================================ */

bool fs_config_exists(const char *key)
{
    if (!g_mounted || key == NULL) return false;
    return (find_entry(key) >= 0);
}

/* ================================================================
 * 公开接口: 配置区 — 删除
 * ================================================================ */

bool fs_config_delete(const char *key)
{
    if (!g_mounted || key == NULL) {
        LOG_ERR("fs_config_delete: invalid args");
        return false;
    }

    int idx = find_entry(key);
    if (idx < 0) {
        LOG_ERR("fs_config_delete: key '%s' not found", key);
        return false;
    }

    /* 清除有效标志 */
    g_dir.entries[idx].flags &= ~FS_FLAG_VALID;

    /* 重新统计有效条目数 */
    uint8_t count = 0;
    for (int i = 0; i < FS_MAX_ENTRIES; i++) {
        if (g_dir.entries[i].flags & FS_FLAG_VALID) count++;
    }
    g_dir.entry_count = count;

    if (!flush_directory()) {
        LOG_ERR("fs_config_delete: flush failed");
        return false;
    }

    LOG("fs_config_delete '%s' done", key);
    return true;
}

/* ================================================================
 * 公开接口: 配置区 — 格式化 (恢复出厂设置)
 * ================================================================ */

void fs_config_format(void)
{
    if (!g_mounted) {
        LOG_ERR("fs_config_format: not mounted");
        return;
    }

    LOG("Factory reset: formatting config partition...");
    format_internal();

    LOG("Factory reset complete");
}

/* ================================================================
 * 公开接口: 资源区 — 打开 (返回数据指针)
 * ================================================================ */

void* fs_res_open(const char *path)
{
    if (path == NULL) return NULL;

    /* 资源区为预烧录只读区域, 当前未实现资源加载器,
     * 返回 NULL 表示资源不可用。 */
    LOG("fs_res_open '%s': resource not loaded (stub)", path);
    return NULL;
}

/* ================================================================
 * 公开接口: 资源区 — 查询大小
 * ================================================================ */

size_t fs_res_size(const char *path)
{
    if (path == NULL) return 0;

    LOG("fs_res_size '%s': resource not loaded (stub)", path);
    return 0;
}

/* ================================================================
 * 公开接口: 资源区 — 检测存在
 * ================================================================ */

bool fs_res_exists(const char *path)
{
    if (path == NULL) return false;

    LOG("fs_res_exists '%s': resource not loaded (stub)", path);
    return false;
}

/* ================================================================
 * 公开接口: 通用文件操作 (调试用)
 * ================================================================ */

bool fs_file_read(const char *path, void *buf, size_t size)
{
    return fs_config_load(path, buf, size);
}

bool fs_file_write(const char *path, const void *buf, size_t size)
{
    return fs_config_save(path, buf, size);
}

bool fs_file_remove(const char *path)
{
    return fs_config_delete(path);
}

/* ================================================================
 * 公开接口: 空间查询
 * ================================================================ */

uint32_t fs_get_total_config(void)
{
    return (uint32_t)(CONFIG_DATA_END - CONFIG_DATA_START);
}

uint32_t fs_get_free_config(void)
{
    uint32_t used = 0;

    for (int i = 0; i < FS_MAX_ENTRIES; i++) {
        if (g_dir.entries[i].flags & FS_FLAG_VALID) {
            used += g_dir.entries[i].size;
        }
    }

    uint32_t total = fs_get_total_config();
    return (used < total) ? (total - used) : 0;
}
