#include "ofs.h"

#include "esp_partition.h"
#include "esp_log.h"
#include "rom/md5_hash.h"

/* ✅ 用你已有的 ota.h（里面声明了 md5_to_hex） */
/* ===================== MD5 → HEX 字符串 ===================== */
static void md5_to_hex(const uint8_t md5[16], char out[33])
{
    for (int i = 0; i < 16; i++)
    {
        sprintf(out + i * 2, "%02x", md5[i]);
    }
    out[32] = 0;
}

static const char *TAG = "OFS";

typedef struct {
    const esp_partition_t *partition;
    uint8_t md5[16];
    size_t written;
} ofs_ctx_t;

static ofs_ctx_t s_ofs = {0};

esp_err_t ofs_upload_handler(httpd_req_t *req)
{
    if (req->content_len <= 0 || req->content_len > 0x200000) {
        ESP_LOGE(TAG, "Invalid size");
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }

    s_ofs.partition = esp_partition_find_first(
        ESP_PARTITION_TYPE_DATA,
        ESP_PARTITION_SUBTYPE_ANY,
        "storage"
    );

    if (!s_ofs.partition) {
        ESP_LOGE(TAG, "storage partition not found");
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Erasing storage...");
    if (esp_partition_erase_range(s_ofs.partition, 0, 0x200000) != ESP_OK) {
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }

    struct MD5Context md5_ctx;
    MD5Init(&md5_ctx);

    char buf[1024];
    s_ofs.written = 0;

    static int s_last_progress = -1;

    while (s_ofs.written < req->content_len) {
        int ret = httpd_req_recv(req, buf, sizeof(buf));
        if (ret <= 0) {
            ESP_LOGE(TAG, "recv error");
            httpd_resp_send_500(req);
            return ESP_FAIL;
        }

        esp_partition_write(s_ofs.partition, s_ofs.written, buf, ret);
        MD5Update(&md5_ctx, (unsigned char *)buf, ret);
        s_ofs.written += ret;

        int cur =
            (int)((uint64_t)s_ofs.written * 100 / req->content_len);

        if (cur != s_last_progress) {
            s_last_progress = cur;
            ESP_LOGI(TAG, "OFS progress %d%%", cur);
        }
    }

    MD5Final(s_ofs.md5, &md5_ctx);

    char md5_str[33];
    md5_to_hex(s_ofs.md5, md5_str);
    ESP_LOGI(TAG, "Write done, MD5=%s", md5_str);

    char resp[128];
    snprintf(resp, sizeof(resp),
              "{\"result\":\"ok\",\"md5\":\"%s\"}",
              md5_str);

    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, resp, strlen(resp));
    return ESP_OK;
}