#pragma once

#include "esp_http_server.h"

esp_err_t ota_upload_handler(httpd_req_t *req);
esp_err_t ota_verify_handler(httpd_req_t *req);
esp_err_t ota_apply_handler(httpd_req_t *req);
esp_err_t ota_reboot_handler(httpd_req_t *req);
esp_err_t ota_progress_handler(httpd_req_t *req);
