#ifndef LFS_DRIVER_H
#define LFS_DRIVER_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include "esp_err.h"
#include "esp_http_server.h"




typedef struct {
    size_t total_bytes;
    size_t used_bytes;
    size_t free_bytes;
} lfs_usage_t;

// 初始化并挂载 LittleFS（失败时自动格式化）
void lfs_init(void);

//查文件
/* ✅ 新增 */
void lfs_verify_cmdnotes_size(void);

// 写入文本到指定路径（自动创建文件，追加写入）
void lfs_write_file(const char *path, const char *data);

// 从指定路径读取文本（最多读取 127 字节并打印）
void lfs_read_file(const char *path, char *buf);

// 判断指定路径的文件是否存在
bool lfs_file_exists(const char *path);

// 删除指定路径的文件
esp_err_t lfs_delete_file(const char *path);

// 获取文件系统空间使用情况
lfs_usage_t lfs_get_usage(void);

//
esp_err_t lfs_stream_file_to_http(const char *path, httpd_req_t *req);

//覆盖写
esp_err_t lfs_write_file_overwrite(const char *path, const char *data);

#ifdef __cplusplus
}
#endif

#endif // LFS_DRIVER_H
