#include "ota.h"

#include "esp_http_server.h"
#include "esp_ota_ops.h"
#include "esp_partition.h"
#include "esp_log.h"
#include "esp_system.h"
#include "rom/md5_hash.h"

static const char *TAG = "OTA";

typedef struct
{
    esp_ota_handle_t handle;
    const esp_partition_t *partition;
    uint8_t md5[16];
    size_t written;
    int progress;   
} ota_ctx_t;

static ota_ctx_t s_ota = {0};



/* ===================== MD5 → HEX 字符串 ===================== */
static void md5_to_hex(const uint8_t md5[16], char out[33])
{
    for (int i = 0; i < 16; i++)
    {
        sprintf(out + i * 2, "%02x", md5[i]);
    }
    out[32] = 0;
}

/* ===================== 上传固件（MD5） ===================== */
esp_err_t ota_upload_handler(httpd_req_t *req)
{
    if (req->content_len <= 0)
    {
        ESP_LOGE(TAG, "Invalid content length");
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }

    s_ota.partition = esp_ota_get_next_update_partition(NULL);
    if (!s_ota.partition)
    {
        ESP_LOGE(TAG, "No OTA partition found");
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }

    if (esp_ota_begin(s_ota.partition, req->content_len, &s_ota.handle) != ESP_OK)
    {
        ESP_LOGE(TAG, "esp_ota_begin failed");
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }

    struct MD5Context md5_ctx;
    MD5Init(&md5_ctx);

    s_ota.written = 0;
    s_ota.progress = 0;

    static int s_last_progress = -1;
    s_last_progress = -1;

    char buf[1024];
    int ret;

    while (s_ota.written < req->content_len)
    {
        ret = httpd_req_recv(req, buf, sizeof(buf));
        if (ret <= 0)
        {
            ESP_LOGE(TAG, "recv error");
            esp_ota_end(s_ota.handle);
            httpd_resp_send_500(req);
            return ESP_FAIL;
        }

        esp_ota_write(s_ota.handle, buf, ret);
        MD5Update(&md5_ctx, (unsigned char *)buf, ret);

        s_ota.written += ret;

        /* ✅ 只在百分比变化时更新 */
        int cur =
            (int)((uint64_t)s_ota.written * 100 / req->content_len);

        if (cur != s_last_progress)
        {
            s_ota.progress = cur;
            s_last_progress = cur;
            ESP_LOGI(TAG, "OTA progress %d%%", cur);
        }
    }

    MD5Final(s_ota.md5, &md5_ctx);
    esp_ota_end(s_ota.handle);

    char md5_str[33];
    md5_to_hex(s_ota.md5, md5_str);
    ESP_LOGI(TAG, "OTA upload MD5 = %s", md5_str);

    char resp[128];
    snprintf(resp, sizeof(resp),
             "{\"result\":\"ok\",\"md5\":\"%s\"}",
             md5_str);

    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, resp, strlen(resp));
    return ESP_OK;
}

esp_err_t ota_progress_handler(httpd_req_t *req)
{
    char resp[64];
    snprintf(resp, sizeof(resp),
             "{\"progress\":%d}", s_ota.progress);

    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, resp, strlen(resp));
    return ESP_OK;
}

/* ===================== Flash 回读 MD5 校验 ===================== */
static bool ota_verify_flash(uint8_t out_md5[16])
{
    if (!s_ota.partition || s_ota.written == 0)
        return false;

    uint8_t buf[1024];

    struct MD5Context md5_ctx;
    MD5Init(&md5_ctx);

    for (size_t offset = 0; offset < s_ota.written; offset += sizeof(buf))
    {
        size_t len = sizeof(buf);
        if (offset + len > s_ota.written)
            len = s_ota.written - offset;

        if (esp_partition_read(s_ota.partition, offset, buf, len) != ESP_OK)
            return false;

        MD5Update(&md5_ctx, buf, len);
    }

    MD5Final(out_md5, &md5_ctx);
    return memcmp(out_md5, s_ota.md5, 16) == 0;
}

/* ===================== 校验接口（返回 Flash MD5） ===================== */
esp_err_t ota_verify_handler(httpd_req_t *req)
{
    uint8_t md5_calc[16];

    if (!ota_verify_flash(md5_calc))
    {
        ESP_LOGE(TAG, "Flash MD5 mismatch");
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }

    /* 串口打印 Flash MD5 */
    char md5_str[33];
    md5_to_hex(md5_calc, md5_str);
    ESP_LOGI(TAG, "Flash MD5 = %s", md5_str);

    /* 返回 JSON */
    char resp[128];
    snprintf(resp, sizeof(resp),
             "{\"result\":\"ok\",\"md5\":\"%s\"}",
             md5_str);

    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, resp, strlen(resp));
    return ESP_OK;
}

/* ===================== 应用新固件 ===================== */
esp_err_t ota_apply_handler(httpd_req_t *req)
{
    if (!s_ota.partition)
    {
        ESP_LOGE(TAG, "No OTA partition");
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }

    if (esp_ota_set_boot_partition(s_ota.partition) != ESP_OK)
    {
        ESP_LOGE(TAG, "Set boot partition failed");
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }
    else
    {
        ESP_LOGI(TAG, "OTA apply success");
        httpd_resp_send(req, "{\"result\":\"ok\"}", strlen("{\"result\":\"ok\"}"));
        return ESP_OK;
    }
}

/* ===================== 重启 ===================== */
esp_err_t ota_reboot_handler(httpd_req_t *req)
{
    ESP_LOGI(TAG, "Rebooting...");
    httpd_resp_send(req, "{\"result\":\"reboot\"}", strlen("{\"result\":\"reboot\"}"));
    vTaskDelay(pdMS_TO_TICKS(500));
    esp_restart();
    return ESP_OK;
}
