/* Local ROM transfer portal; no Internet or cloud service is involved. */
#include "tamago_transfer.h"
#include "tamago_dns_server.h"
#include "t7_platform.h"
#include "t7_rom_store.h"
#include "t7_save_bundle.h"
#include "esp_event.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_netif.h"
#include "esp_random.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

extern const char transfer_html_start[] asm("_binary_tamago_transfer_html_start");

static const char *TAG = "tama_transfer";
static httpd_handle_t s_http;
static dns_server_handle_t s_dns;
/* The HTTP server handles one request at a time. Keep the transfer buffer out
 * of its task stack on this RAM-constrained board. */
static uint8_t s_upload_chunk[1024];

static esp_err_t root_get(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/html; charset=utf-8");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    return httpd_resp_send(req, transfer_html_start, HTTPD_RESP_USE_STRLEN);
}

static esp_err_t status_get(httpd_req_t *req)
{
    char json[112];
    size_t used = 0;
    used += snprintf(json + used, sizeof(json) - used, "{\"slots\":[");
    for (uint8_t slot = 0; slot < T7_ROM_SLOT_COUNT; slot++) {
        used += snprintf(json + used, sizeof(json) - used, "%s%s",
                         slot ? "," : "", t7_rom_store_has(slot) ? "true" : "false");
    }
    snprintf(json + used, sizeof(json) - used, "]}");
    httpd_resp_set_type(req, "application/json; charset=utf-8");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    return httpd_resp_send(req, json, HTTPD_RESP_USE_STRLEN);
}

static esp_err_t upload_post(httpd_req_t *req)
{
    char query[32], value[8];
    if (httpd_req_get_url_query_str(req, query, sizeof(query)) != ESP_OK ||
        httpd_query_key_value(query, "id", value, sizeof(value)) != ESP_OK) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "缺少 ROM 编号");
        return ESP_FAIL;
    }
    char *end = NULL;
    unsigned long parsed = strtoul(value, &end, 10);
    if (!value[0] || *end || parsed >= T7_ROM_SLOT_COUNT ||
        req->content_len != t7_rom_store_expected_size((uint8_t)parsed)) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "ROM 编号或文件大小不符");
        return ESP_FAIL;
    }
    uint8_t slot = (uint8_t)parsed;
    t7_rom_writer_t writer;
    if (!t7_rom_store_begin(slot, &writer)) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "闪存准备失败");
        return ESP_FAIL;
    }
    size_t remaining = t7_rom_store_expected_size(slot);
    int timeouts = 0;
    while (remaining) {
        size_t wanted = remaining < sizeof(s_upload_chunk)
                        ? remaining : sizeof(s_upload_chunk);
        int received = httpd_req_recv(req, (char *)s_upload_chunk, wanted);
        if (received == HTTPD_SOCK_ERR_TIMEOUT && ++timeouts <= 3) continue;
        if (received <= 0 || !t7_rom_store_write(&writer, s_upload_chunk,
                                                   (size_t)received)) {
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR,
                                "传输中断或写入失败，请重试");
            return ESP_FAIL;
        }
        timeouts = 0;
        remaining -= (size_t)received;
    }
    if (!t7_rom_store_finish(&writer)) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST,
                            "ROM 校验失败，请确认文件对应型号");
        return ESP_FAIL;
    }
    httpd_resp_set_type(req, "text/plain; charset=utf-8");
    return httpd_resp_sendstr(req, "导入成功");
}

static esp_err_t save_get(httpd_req_t *req)
{
    uint8_t *data = malloc(T7_SAVE_BUNDLE_MAX);
    size_t size = 0;
    if (!data) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "内存不足");
        return ESP_FAIL;
    }
    bool ready = t7_platform_export_save(data, T7_SAVE_BUNDLE_MAX, &size);
    if (!ready) {
        free(data);
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST,
                            "暂无可导出的存档，请先导入主游戏并保存宠物");
        return ESP_FAIL;
    }
    httpd_resp_set_type(req, "application/octet-stream");
    httpd_resp_set_hdr(req, "Content-Disposition",
                       "attachment; filename=\"TamaGo-save.tgsave\"");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    esp_err_t err = httpd_resp_send(req, (const char *)data, size);
    free(data);
    return err;
}

static esp_err_t save_post(httpd_req_t *req)
{
    if (req->content_len < T7_SAVE_BUNDLE_HEADER + T7_SAVE_BUNDLE_EEPROM ||
        req->content_len > T7_SAVE_BUNDLE_MAX) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "存档文件大小不符");
        return ESP_FAIL;
    }
    uint8_t *data = malloc(req->content_len);
    if (!data) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "内存不足");
        return ESP_FAIL;
    }
    size_t received_total = 0;
    int timeouts = 0;
    while (received_total < req->content_len) {
        size_t wanted = req->content_len - received_total;
        if (wanted > 1024) wanted = 1024;
        int received = httpd_req_recv(req, (char *)data + received_total, wanted);
        if (received == HTTPD_SOCK_ERR_TIMEOUT && ++timeouts <= 3) continue;
        if (received <= 0) {
            free(data);
            httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "存档传输中断");
            return ESP_FAIL;
        }
        timeouts = 0;
        received_total += received;
    }
    t7_save_import_result_t result = t7_platform_import_save(data, received_total);
    free(data);
    if (result != T7_SAVE_IMPORT_OK) {
        const char *reason = result == T7_SAVE_IMPORT_FORMAT ? "存档损坏或版本不支持" :
                             result == T7_SAVE_IMPORT_ROM ? "存档与主游戏 ROM 不匹配" :
                             "写入存档失败，请重试";
        httpd_resp_send_err(req, result == T7_SAVE_IMPORT_STORAGE
                            ? HTTPD_500_INTERNAL_SERVER_ERROR : HTTPD_400_BAD_REQUEST,
                            reason);
        return ESP_FAIL;
    }
    httpd_resp_set_type(req, "text/plain; charset=utf-8");
    return httpd_resp_sendstr(req, "存档已导入；点击完成并启动游戏");
}

static void restart_task(void *arg)
{
    (void)arg;
    vTaskDelay(pdMS_TO_TICKS(1000));
    esp_restart();
}

static esp_err_t finish_post(httpd_req_t *req)
{
    if (!t7_rom_store_has(0)) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "请先导入游戏 ROM");
        return ESP_FAIL;
    }
    if (!t7_platform_set_transfer_mode(T7_TRANSFER_GAME)) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "无法保存启动状态");
        return ESP_FAIL;
    }
    if (xTaskCreate(restart_task, "tama_restart", 2048, NULL, 4, NULL) != pdPASS) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "无法重启设备");
        return ESP_FAIL;
    }
    return httpd_resp_sendstr(req, "设备即将启动游戏");
}

static esp_err_t redirect_404(httpd_req_t *req, httpd_err_code_t error)
{
    (void)error;
    httpd_resp_set_status(req, "303 See Other");
    httpd_resp_set_hdr(req, "Location", "/");
    return httpd_resp_sendstr(req, "Open Tama-Go transfer page");
}

static bool start_http(void)
{
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.stack_size = 4096;
    config.max_uri_handlers = 6;
    config.max_open_sockets = 4;
    config.lru_purge_enable = true;
    config.recv_wait_timeout = 30;
    config.send_wait_timeout = 30;
    ESP_LOGI(TAG, "HTTP start with %lu bytes free", (unsigned long)esp_get_free_heap_size());
    esp_err_t err = httpd_start(&s_http, &config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "HTTP start: %s", esp_err_to_name(err));
        return false;
    }
    const httpd_uri_t routes[] = {
        { .uri = "/", .method = HTTP_GET, .handler = root_get },
        { .uri = "/api/status", .method = HTTP_GET, .handler = status_get },
        { .uri = "/api/upload", .method = HTTP_POST, .handler = upload_post },
        { .uri = "/api/save", .method = HTTP_GET, .handler = save_get },
        { .uri = "/api/save", .method = HTTP_POST, .handler = save_post },
        { .uri = "/api/finish", .method = HTTP_POST, .handler = finish_post },
    };
    for (size_t i = 0; i < sizeof(routes) / sizeof(routes[0]); i++) {
        err = httpd_register_uri_handler(s_http, &routes[i]);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "HTTP route %s: %s", routes[i].uri, esp_err_to_name(err));
            return false;
        }
    }
    err = httpd_register_err_handler(s_http, HTTPD_404_NOT_FOUND, redirect_404);
    if (err != ESP_OK) ESP_LOGE(TAG, "HTTP redirect: %s", esp_err_to_name(err));
    return err == ESP_OK;
}

static bool credentials(char *ssid, size_t ssid_size,
                        char *password, size_t password_size)
{
    uint8_t mac[6];
    if (esp_read_mac(mac, ESP_MAC_WIFI_SOFTAP) != ESP_OK ||
        snprintf(ssid, ssid_size, "TamaGo-%02X%02X", mac[4], mac[5]) >= ssid_size)
        return false;
    nvs_handle_t nvs;
    if (nvs_open("tama_web", NVS_READWRITE, &nvs) != ESP_OK) return false;
    size_t length = password_size;
    bool found = nvs_get_str(nvs, "password", password, &length) == ESP_OK &&
                 length == 13;
    if (!found) {
        if (password_size < 13) { nvs_close(nvs); return false; }
        snprintf(password, password_size, "TG%08lx%02X",
                 (unsigned long)esp_random(), mac[5]);
        esp_err_t err = nvs_set_str(nvs, "password", password);
        if (err == ESP_OK) err = nvs_commit(nvs);
        if (err != ESP_OK) { nvs_close(nvs); return false; }
    }
    nvs_close(nvs);
    return true;
}

bool tamago_transfer_start(char *ssid, size_t ssid_size,
                           char *password, size_t password_size)
{
    if (!credentials(ssid, ssid_size, password, password_size)) return false;
    esp_err_t err = esp_netif_init();
    if (err != ESP_OK) { ESP_LOGE(TAG, "Netif init: %s", esp_err_to_name(err)); return false; }
    err = esp_event_loop_create_default();
    if (err != ESP_OK) { ESP_LOGE(TAG, "Event loop: %s", esp_err_to_name(err)); return false; }
    esp_netif_t *ap = esp_netif_create_default_wifi_ap();
    wifi_init_config_t init = WIFI_INIT_CONFIG_DEFAULT();
    if (!ap) { ESP_LOGE(TAG, "AP netif creation failed"); return false; }
    err = esp_wifi_init(&init);
    if (err != ESP_OK) { ESP_LOGE(TAG, "Wi-Fi init: %s", esp_err_to_name(err)); return false; }
    err = esp_wifi_set_storage(WIFI_STORAGE_RAM);
    if (err != ESP_OK) { ESP_LOGE(TAG, "Wi-Fi storage: %s", esp_err_to_name(err)); return false; }
    wifi_config_t wifi = {0};
    snprintf((char *)wifi.ap.ssid, sizeof(wifi.ap.ssid), "%s", ssid);
    wifi.ap.ssid_len = strlen(ssid);
    snprintf((char *)wifi.ap.password, sizeof(wifi.ap.password), "%s", password);
    wifi.ap.channel = 1;
    wifi.ap.max_connection = 2;
    wifi.ap.authmode = WIFI_AUTH_WPA2_PSK;
    err = esp_wifi_set_mode(WIFI_MODE_AP);
    if (err != ESP_OK) { ESP_LOGE(TAG, "Wi-Fi AP mode: %s", esp_err_to_name(err)); return false; }
    err = esp_wifi_set_config(WIFI_IF_AP, &wifi);
    if (err != ESP_OK) { ESP_LOGE(TAG, "Wi-Fi AP config: %s", esp_err_to_name(err)); return false; }
    err = esp_wifi_start();
    if (err != ESP_OK) { ESP_LOGE(TAG, "Wi-Fi AP start: %s", esp_err_to_name(err)); return false; }

    /* The default AP starts DHCP on Wi-Fi start. Restarting it here left some
     * phones waiting indefinitely for an address. DNS handles portal probes. */
    if (!start_http()) return false;
    dns_server_config_t dns = DNS_SERVER_CONFIG_SINGLE("*", "WIFI_AP_DEF");
    s_dns = start_dns_server(&dns);
    if (!s_dns) ESP_LOGW(TAG, "DNS redirect unavailable; use 192.168.4.1");
    ESP_LOGI(TAG, "Transfer portal ready on %s at 192.168.4.1", ssid);
    return true;
}
