#include "lfs_driver.h"
#include "lfs.h"
#include "esp_partition.h"
#include "spi_flash.h"
#include "esp_log.h"   // ✅ 必须
#include <string.h>
#include <stdbool.h>
#include "esp_http_server.h"

static lfs_t lfs;
static struct lfs_config cfg;
static const esp_partition_t *lfs_partition = NULL;

static int user_lfs_read(const struct lfs_config *c, lfs_block_t block,
                         lfs_off_t off, void *buffer, lfs_size_t size) {
    uint32_t addr = lfs_partition->address + block * c->block_size + off;
    return spi_flash_read(addr, buffer, size) == ESP_OK ? 0 : -1;
}

static int user_lfs_prog(const struct lfs_config *c, lfs_block_t block,
                         lfs_off_t off, const void *buffer, lfs_size_t size) {
    uint32_t addr = lfs_partition->address + block * c->block_size + off;
    return spi_flash_write(addr, (const uint8_t *)buffer, size) == ESP_OK ? 0 : -1;
}

static int user_lfs_erase(const struct lfs_config *c, lfs_block_t block) {
    uint32_t addr = lfs_partition->address + block * c->block_size;
    return spi_flash_erase_sector(addr / 4096) == ESP_OK ? 0 : -1;
}

static int user_lfs_sync(const struct lfs_config *c) {
    return 0;
}


void lfs_init(void) {
    lfs_partition = esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_SPIFFS, "storage");
    if (!lfs_partition) {
        printf("❌ Failed to find partition 'storage'\n");
        return;
    }

    cfg.read = user_lfs_read;
    cfg.prog = user_lfs_prog;
    cfg.erase = user_lfs_erase;
    cfg.sync = user_lfs_sync;
    cfg.read_size = 256;
    cfg.prog_size = 256;
    cfg.block_size = 4096;
    cfg.block_count = lfs_partition->size / cfg.block_size;
    cfg.cache_size = 256;
    cfg.lookahead_size = 16;
    cfg.block_cycles = 500;

    // printf("!!!!!!!!!!!!!!!!!!!!!!!!!!!!!LFS start addr = 0x%08X\n", lfs_partition->address);
    // printf("LFS partition: label=%s addr=0x%08X size=0x%08X\n",lfs_partition->label,lfs_partition->address,lfs_partition->size);
    // printf("block_count: cfg=%d, partition_blocks=%d\n",cfg.block_count,lfs_partition->size / 4096);

    int err = lfs_mount(&lfs, &cfg);
    if (err) {
        printf("⚠️ Mount failed, formatting...\n");
        lfs_format(&lfs, &cfg);
        err = lfs_mount(&lfs, &cfg);
    }

    if (err == 0) {
        // printf("✅ LittleFS mounted successfully.\n");

        // 🔍 打印根目录文件清单
        // lfs_dir_t dir;
        // struct lfs_info info;

        // if (lfs_dir_open(&lfs, &dir, "/") == 0) {
        //     printf("📂 File list in root directory:\n");
        //     while (lfs_dir_read(&lfs, &dir, &info) > 0) {
        //         if (strcmp(info.name, ".") == 0 || strcmp(info.name, "..") == 0) continue;
        //         printf(" - %s (%s)\n", info.name,
        //                (info.type == LFS_TYPE_REG) ? "file" :
        //                (info.type == LFS_TYPE_DIR) ? "dir" : "unknown");
        //     }
        //     lfs_dir_close(&lfs, &dir);
        // } else {
        //     printf("❌ Failed to open root directory.\n");
        // }
    } else {
        printf("❌ Failed to mount LittleFS after format.\n");
    }
}


void lfs_write_file(const char *path, const char *data) {
    lfs_file_t file;
    int err = lfs_file_open(&lfs, &file, path, LFS_O_WRONLY | LFS_O_CREAT | LFS_O_APPEND);
    if (err == LFS_ERR_NOENT) {
        int create_err = lfs_file_open(&lfs, &file, path, LFS_O_WRONLY | LFS_O_CREAT);
        if (create_err == 0) {
            lfs_file_close(&lfs, &file);
            err = lfs_file_open(&lfs, &file, path, LFS_O_WRONLY | LFS_O_CREAT | LFS_O_APPEND);
        }
    }
    if (err < 0) return;

    lfs_file_write(&lfs, &file, data, strlen(data));
    lfs_file_close(&lfs, &file);
}

void lfs_read_file(const char *path, char *buf)
{
    lfs_file_t file;

    if (!buf)
        return;

    if (lfs_file_open(&lfs, &file, path, LFS_O_RDONLY) < 0)
        return;

    memset(buf, 0, 128);
    lfs_file_read(&lfs, &file, buf, 127);
    lfs_file_close(&lfs, &file);
}

bool lfs_file_exists(const char *path) {
    lfs_file_t file;
    int err = lfs_file_open(&lfs, &file, path, LFS_O_RDONLY);
    if (err == 0) {
        lfs_file_close(&lfs, &file);
        return true;
    }
    return false;
}

esp_err_t lfs_delete_file(const char *path) {
    int err = lfs_remove(&lfs, path);
    return (err == 0) ? ESP_OK : ESP_FAIL;
}

lfs_usage_t lfs_get_usage(void) {
    lfs_usage_t usage = {0};
    lfs_ssize_t used_blocks = lfs_fs_size(&lfs);
    if (used_blocks < 0) return usage;

    usage.total_bytes = cfg.block_count * cfg.block_size;
    usage.used_bytes = used_blocks * cfg.block_size;
    usage.free_bytes = usage.total_bytes - usage.used_bytes;
    return usage;
}

esp_err_t lfs_stream_file_to_http(const char *path, httpd_req_t *req)
{
    lfs_file_t file;
    if (lfs_file_open(&lfs, &file, path, LFS_O_RDONLY) < 0) {
        return ESP_FAIL;
    }

    /* 1️⃣ Content-Type（必须） */
    if (strstr(path, ".json"))
        httpd_resp_set_type(req, "application/json");
    else if (strstr(path, ".html"))
        httpd_resp_set_type(req, "text/html");
    else
        httpd_resp_set_type(req, "application/octet-stream");

    /* 2️⃣ 分块发送（唯一安全方式） */
    char buf[512];
    lfs_ssize_t n;

    while ((n = lfs_file_read(&lfs, &file, buf, sizeof(buf))) > 0) {
        if (httpd_resp_send_chunk(req, buf, n) != ESP_OK) {
            break;
        }
    }

    lfs_file_close(&lfs, &file);
    httpd_resp_send_chunk(req, NULL, 0);
    return ESP_OK;
}

esp_err_t lfs_write_file_overwrite(const char *path, const char *data)
{
    lfs_file_t file;
    int err;

    err = lfs_file_open(&lfs, &file, path,
                        LFS_O_WRONLY | LFS_O_CREAT | LFS_O_TRUNC);
    if (err < 0) {
        ESP_LOGE("LFS", "open fail: %s", path);
        return ESP_FAIL;
    }

    size_t len = strlen(data);
    size_t written = 0;

    while (written < len) {
        lfs_ssize_t n = lfs_file_write(&lfs, &file, data + written, len - written);
        if (n < 0) {
            lfs_file_close(&lfs, &file);
            return ESP_FAIL;
        }
        written += n;
    }

    lfs_file_sync(&lfs, &file);   // ✅ 必须
    lfs_file_close(&lfs, &file);

    ESP_LOGI("LFS", "saved %d bytes", (int)written);
    return ESP_OK;
}

void lfs_verify_cmdnotes_size(void)
{
    lfs_file_t file;
    int err;

    err = lfs_file_open(&lfs, &file, "/cmdnotes.json", LFS_O_RDONLY);
    if (err < 0) {
        printf("❌ cmdnotes.json not found\n");
        return;
    }

    lfs_ssize_t size = lfs_file_size(&lfs, &file);
    printf("📏 cmdnotes.json size = %d bytes\n", size);

    /* 读最后 64 字节 */
    char tail[65] = {0};
    lfs_file_seek(&lfs, &file, -64, LFS_SEEK_END);
    lfs_file_read(&lfs, &file, tail, 64);
    printf("📌 last 64 bytes:\n%s\n", tail);

    lfs_file_close(&lfs, &file);
}