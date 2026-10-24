/* AI Passport host for the portable T7Emu core. The user-imported ROM is
 * mapped from the data partition, so bank changes use no RAM. */
#include "t7_platform.h"
#include "t7_core/pebble_compat.h"
#include "t7_core/t7.h"
#include "t7_core/t7_eeprom.h"
#include "t7_settings_menu.h"
#include "t7_rom_store.h"
#include "t7_save_bundle.h"
#include "esp_log.h"
#include "nvs.h"
#include "nvs_flash.h"
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

static const char *TAG = "t7_host";
static nvs_handle_t s_nvs;
static bool s_nvs_ready;
static uint32_t s_rom_hash;
static const uint8_t *s_rom;

static uint32_t fnv1a(const uint8_t *p, size_t n)
{
    uint32_t h = 2166136261u;
    for (size_t i = 0; i < n; i++) h = (h ^ p[i]) * 16777619u;
    return h;
}

uint32_t t7_platform_rom_hash(void)
{
    if (s_rom && !s_rom_hash) s_rom_hash = fnv1a(s_rom, 655360u);
    return s_rom_hash;
}

void t7_platform_set_rom(const uint8_t *rom)
{
    s_rom = rom;
    s_rom_hash = 0;
}

bool t7_platform_init(void)
{
    esp_err_t err = nvs_flash_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "NVS init: %s", esp_err_to_name(err));
        return false;
    }
    err = nvs_open("tamago", NVS_READWRITE, &s_nvs);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "NVS open: %s", esp_err_to_name(err));
        return false;
    }
    s_nvs_ready = true;
    ESP_LOGI(TAG, "NVS ready");
    return true;
}

void tama_log(const char *fmt, ...)
{
    char buf[160];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    ESP_LOGI(TAG, "%s", buf);
}

void t7_compat_set_rom_path(const char *path) { (void)path; }
void t7_compat_set_eeprom_path(const char *path) { (void)path; }
ResHandle resource_get_handle(int id) { return s_rom && id == RESOURCE_ID_TAMA_ROM ? 1 : 0; }
size_t resource_size(ResHandle h) { return s_rom && h == 1 ? 655360u : 0; }
size_t resource_load_byte_range(ResHandle h, size_t off, uint8_t *out, size_t n)
{
    size_t size = resource_size(h);
    if (off >= size) return 0;
    if (n > size - off) n = size - off;
    memcpy(out, s_rom + off, n);
    return n;
}
const uint8_t *t7_compat_rom_mapped(void) { return s_rom; }

static bool page_key(uint32_t key, char out[8])
{
    if (key < 100 || key >= 116) return false;
    snprintf(out, 8, "ep%02u", (unsigned)(key - 100));
    return true;
}

bool persist_exists(uint32_t key)
{
    char name[8];
    size_t n = 0;
    return s_nvs_ready && page_key(key, name) &&
           nvs_get_blob(s_nvs, name, NULL, &n) == ESP_OK && n == 256;
}

int persist_read_data(uint32_t key, void *buf, size_t size)
{
    char name[8];
    if (!s_nvs_ready || !page_key(key, name) || size < 256) return -1;
    size_t n = size;
    return nvs_get_blob(s_nvs, name, buf, &n) == ESP_OK ? (int)n : -1;
}
status_t persist_write_data(uint32_t key, const void *buf, size_t size)
{
    char name[8];
    if (!s_nvs_ready || !page_key(key, name) || size != 256) return -1;
    esp_err_t err = nvs_set_blob(s_nvs, name, buf, size);
    if (err == ESP_OK) err = nvs_commit(s_nvs);
    return err == ESP_OK ? S_SUCCESS : -1;
}
status_t persist_delete(uint32_t key)
{
    char name[8];
    if (!s_nvs_ready || !page_key(key, name)) return -1;
    esp_err_t err = nvs_erase_key(s_nvs, name);
    if (err == ESP_OK) err = nvs_commit(s_nvs);
    return err == ESP_OK || err == ESP_ERR_NVS_NOT_FOUND ? S_SUCCESS : -1;
}
void persist_flush_to_disk(void) {}
int persist_page_count(void) { return 16; }
int persist_page_size(void) { return 256; }
int persist_backup_page(int page, uint8_t *out)
{
    return page >= 0 && page < 16 ? persist_read_data((uint32_t)page + 100, out, 256) : -1;
}
void persist_restore_page(int page, const uint8_t *in)
{
    if (page >= 0 && page < 16) (void)persist_write_data((uint32_t)page + 100, in, 256);
}

typedef struct {
    uint32_t rom_hash;
    uint32_t size;
    uint8_t data[4096];
} saved_state_t;
static saved_state_t s_state;

bool t7_platform_load_state(void)
{
    if (!s_nvs_ready) return false;
    size_t n = sizeof(s_state);
    if (nvs_get_blob(s_nvs, "session", &s_state, &n) != ESP_OK ||
        n < 8 || s_state.rom_hash != t7_platform_rom_hash() ||
        s_state.size > sizeof(s_state.data) || s_state.size != n - 8) return false;
    return t7_deserialize_state(s_state.data, s_state.size);
}

bool t7_platform_save_state(void)
{
    if (!s_nvs_ready) return false;
    t7_eeprom_flush();
    s_state.rom_hash = t7_platform_rom_hash();
    s_state.size = t7_serialize_state(s_state.data, sizeof(s_state.data));
    if (s_state.size == 0 || s_state.size > sizeof(s_state.data)) return false;
    esp_err_t err = nvs_set_blob(s_nvs, "session", &s_state, 8 + s_state.size);
    if (err == ESP_OK) err = nvs_commit(s_nvs);
    if (err != ESP_OK) ESP_LOGE(TAG, "Session save: %s", esp_err_to_name(err));
    return err == ESP_OK;
}

bool t7_platform_export_save(uint8_t *out, size_t capacity, size_t *written)
{
    if (!s_nvs_ready || !out || !written || capacity < T7_SAVE_BUNDLE_MAX ||
        !t7_rom_store_has(0)) return false;
    uint8_t *eeprom = out + T7_SAVE_BUNDLE_HEADER;
    uint8_t *session = eeprom + T7_SAVE_BUNDLE_EEPROM;
    uint16_t page_mask = 0;
    for (unsigned page = 0; page < 16; page++) {
        char key[8];
        snprintf(key, sizeof(key), "ep%02u", page);
        size_t length = 256;
        esp_err_t err = nvs_get_blob(s_nvs, key, eeprom + page * 256, &length);
        if (err == ESP_OK && length == 256) page_mask |= (uint16_t)1u << page;
        else if (err == ESP_ERR_NVS_NOT_FOUND) memset(eeprom + page * 256, 0, 256);
        else return false;
    }
    size_t session_size = 0;
    size_t length = sizeof(s_state);
    esp_err_t err = nvs_get_blob(s_nvs, "session", &s_state, &length);
    if (err == ESP_OK && length >= 8 && s_state.rom_hash == t7_rom_store_hash(0) &&
        s_state.size == length - 8 &&
        s_state.size == t7_serialize_state(NULL, 0) &&
        s_state.data[0] == 'G' && s_state.data[1] == 'M' &&
        s_state.data[2] == 'A' && s_state.data[3] == 'T' &&
        (s_state.data[4] == 1 || s_state.data[4] == 2)) {
        session_size = s_state.size;
        memcpy(session, s_state.data, session_size);
    } else if (err != ESP_OK && err != ESP_ERR_NVS_NOT_FOUND) {
        return false;
    }
    if (!page_mask && !session_size) return false;
    *written = t7_save_bundle_seal(out, capacity, t7_rom_store_hash(0),
                                   page_mask, session_size);
    return *written != 0;
}

t7_save_import_result_t t7_platform_import_save(const uint8_t *data, size_t size)
{
    t7_save_bundle_view_t save;
    if (!t7_save_bundle_parse(data, size, &save) ||
        (save.session_size &&
         (save.session_size != t7_serialize_state(NULL, 0) ||
          save.session[0] != 'G' || save.session[1] != 'M' ||
          save.session[2] != 'A' || save.session[3] != 'T' ||
          (save.session[4] != 1 && save.session[4] != 2))))
        return T7_SAVE_IMPORT_FORMAT;
    if (!s_nvs_ready || !t7_rom_store_has(0) ||
        save.rom_hash != t7_rom_store_hash(0)) return T7_SAVE_IMPORT_ROM;
    for (unsigned page = 0; page < 16; page++) {
        char key[8];
        snprintf(key, sizeof(key), "ep%02u", page);
        esp_err_t err = save.page_mask & ((uint16_t)1u << page)
                        ? nvs_set_blob(s_nvs, key, save.eeprom + page * 256, 256)
                        : nvs_erase_key(s_nvs, key);
        if (err != ESP_OK && err != ESP_ERR_NVS_NOT_FOUND)
            return T7_SAVE_IMPORT_STORAGE;
    }
    esp_err_t err;
    if (save.session_size) {
        s_state.rom_hash = save.rom_hash;
        s_state.size = save.session_size;
        memcpy(s_state.data, save.session, save.session_size);
        err = nvs_set_blob(s_nvs, "session", &s_state, 8 + save.session_size);
    } else {
        err = nvs_erase_key(s_nvs, "session");
    }
    if (err != ESP_OK && err != ESP_ERR_NVS_NOT_FOUND)
        return T7_SAVE_IMPORT_STORAGE;
    return nvs_commit(s_nvs) == ESP_OK
           ? T7_SAVE_IMPORT_OK : T7_SAVE_IMPORT_STORAGE;
}

uint8_t t7_platform_load_figure(void)
{
    uint8_t index = 0;
    if (s_nvs_ready) (void)nvs_get_u8(s_nvs, "figure", &index);
    return index;
}

bool t7_platform_save_figure(uint8_t index)
{
    if (!s_nvs_ready) return false;
    esp_err_t err = nvs_set_u8(s_nvs, "figure", index);
    if (err == ESP_OK) err = nvs_commit(s_nvs);
    if (err != ESP_OK) ESP_LOGE(TAG, "Figure selection save: %s", esp_err_to_name(err));
    return err == ESP_OK;
}

static uint8_t load_percent(const char *key, uint8_t fallback, uint8_t minimum)
{
    uint8_t value = fallback;
    if (s_nvs_ready && nvs_get_u8(s_nvs, key, &value) == ESP_OK &&
        value >= minimum && value <= 100) return value;
    return fallback;
}

uint8_t t7_platform_load_volume(void)
{
    return load_percent("volume", SETTINGS_DEFAULT_VOLUME, 0);
}

uint8_t t7_platform_load_brightness(void)
{
    return load_percent("bright", SETTINGS_DEFAULT_BRIGHTNESS,
                        SETTINGS_MIN_BRIGHTNESS);
}

static bool save_percent(const char *key, uint8_t value, uint8_t minimum)
{
    if (!s_nvs_ready || value < minimum || value > 100) return false;
    esp_err_t err = nvs_set_u8(s_nvs, key, value);
    if (err == ESP_OK) err = nvs_commit(s_nvs);
    if (err != ESP_OK) ESP_LOGE(TAG, "%s save: %s", key, esp_err_to_name(err));
    return err == ESP_OK;
}

bool t7_platform_save_volume(uint8_t percent)
{
    return save_percent("volume", percent, 0);
}

bool t7_platform_save_brightness(uint8_t percent)
{
    return save_percent("bright", percent, SETTINGS_MIN_BRIGHTNESS);
}

uint8_t t7_platform_transfer_mode(void)
{
    uint8_t mode = T7_TRANSFER_GAME;
    if (!s_nvs_ready || nvs_get_u8(s_nvs, "transfer", &mode) != ESP_OK ||
        mode > T7_TRANSFER_CHOICE) return T7_TRANSFER_GAME;
    return mode;
}

bool t7_platform_set_transfer_mode(uint8_t mode)
{
    if (!s_nvs_ready || mode > T7_TRANSFER_CHOICE) return false;
    esp_err_t err = nvs_set_u8(s_nvs, "transfer", mode);
    if (err == ESP_OK) err = nvs_commit(s_nvs);
    if (err != ESP_OK) ESP_LOGE(TAG, "Transfer mode save: %s", esp_err_to_name(err));
    return err == ESP_OK;
}
