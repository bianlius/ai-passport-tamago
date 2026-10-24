/* Fixed raw-flash slots. ROM bytes are supplied by the user, never linked into the app. */
#include "t7_rom_store.h"
#include "t7_figure_identify.h"
#include "esp_log.h"
#include "nvs.h"
#include <stdio.h>
#include <string.h>

#define ROM_SUBTYPE 0x40
#define ROM_META_MAGIC 0x54474d31u
#define FLASH_SECTOR 4096u

typedef struct {
    uint32_t magic;
    uint32_t size;
    uint32_t fnv32;
} rom_meta_t;

static const size_t s_sizes[T7_ROM_SLOT_COUNT] = {
    T7_MAIN_ROM_SIZE,
    T7_FIGURE_SMALL_SIZE, T7_FIGURE_SMALL_SIZE, T7_FIGURE_SMALL_SIZE,
    T7_FIGURE_SMALL_SIZE, T7_FIGURE_SMALL_SIZE, T7_FIGURE_SMALL_SIZE,
    T7_FIGURE_LARGE_SIZE, T7_FIGURE_LARGE_SIZE, T7_FIGURE_LARGE_SIZE,
    T7_FIGURE_LARGE_SIZE, T7_FIGURE_LARGE_SIZE, T7_FIGURE_LARGE_SIZE,
};
static const char *TAG = "tama_rom";
static const esp_partition_t *s_partition;
static nvs_handle_t s_nvs;
static bool s_present[T7_ROM_SLOT_COUNT];
static uint32_t s_hash[T7_ROM_SLOT_COUNT];

static size_t slot_span(uint8_t slot)
{
    return (s_sizes[slot] + FLASH_SECTOR - 1u) & ~(FLASH_SECTOR - 1u);
}

static size_t slot_offset(uint8_t slot)
{
    size_t offset = 0;
    for (uint8_t i = 0; i < slot; i++) offset += slot_span(i);
    return offset;
}

static void slot_key(uint8_t slot, char key[8])
{
    snprintf(key, 8, "rom%02u", slot);
}

static uint32_t fnv_update(uint32_t hash, const uint8_t *data, size_t length)
{
    for (size_t i = 0; i < length; i++) hash = (hash ^ data[i]) * 16777619u;
    return hash;
}

size_t t7_rom_store_expected_size(uint8_t slot)
{
    return slot < T7_ROM_SLOT_COUNT ? s_sizes[slot] : 0;
}

bool t7_rom_store_init(void)
{
    s_partition = esp_partition_find_first(ESP_PARTITION_TYPE_DATA,
                                            (esp_partition_subtype_t)ROM_SUBTYPE,
                                            "roms");
    if (!s_partition || slot_offset(T7_ROM_SLOT_COUNT) > s_partition->size) {
        ESP_LOGE(TAG, "ROM partition missing or too small");
        return false;
    }
    if (nvs_open("tama_rom", NVS_READWRITE, &s_nvs) != ESP_OK) {
        ESP_LOGE(TAG, "ROM metadata NVS unavailable");
        return false;
    }
    for (uint8_t slot = 0; slot < T7_ROM_SLOT_COUNT; slot++) {
        char key[8];
        slot_key(slot, key);
        rom_meta_t meta;
        size_t length = sizeof(meta);
        s_present[slot] = nvs_get_blob(s_nvs, key, &meta, &length) == ESP_OK &&
                          length == sizeof(meta) && meta.magic == ROM_META_MAGIC &&
                          meta.size == s_sizes[slot];
        if (s_present[slot] && slot != 0) {
            uint8_t header[T7_FIGURE_HEADER_SIZE];
            if (esp_partition_read(s_partition, slot_offset(slot), header,
                                   sizeof(header)) != ESP_OK ||
                t7_figure_rom_slot(header, sizeof(header), s_sizes[slot]) != slot)
                s_present[slot] = false;
        }
        s_hash[slot] = s_present[slot] ? meta.fnv32 : 0;
    }
    ESP_LOGI(TAG, "ROM partition %u bytes; main %s",
             (unsigned)s_partition->size, s_present[0] ? "ready" : "missing");
    return true;
}

bool t7_rom_store_has(uint8_t slot)
{
    return slot < T7_ROM_SLOT_COUNT && s_present[slot];
}

uint32_t t7_rom_store_hash(uint8_t slot)
{
    return slot < T7_ROM_SLOT_COUNT && s_present[slot] ? s_hash[slot] : 0;
}

uint16_t t7_rom_store_figure_mask(void)
{
    uint16_t mask = 0;
    for (uint8_t slot = 1; slot < T7_ROM_SLOT_COUNT; slot++)
        if (s_present[slot]) mask |= (uint16_t)1u << (slot - 1);
    return mask;
}

bool t7_rom_store_map(uint8_t slot, const uint8_t **ptr,
                      esp_partition_mmap_handle_t *handle)
{
    if (!ptr || !handle || !t7_rom_store_has(slot)) return false;
    const void *mapped = NULL;
    esp_err_t err = esp_partition_mmap(s_partition, slot_offset(slot), s_sizes[slot],
                                        ESP_PARTITION_MMAP_DATA, &mapped, handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Map ROM %u: %s", slot, esp_err_to_name(err));
        return false;
    }
    *ptr = mapped;
    return true;
}

bool t7_rom_store_begin(uint8_t slot, t7_rom_writer_t *writer)
{
    if (!writer || !s_partition || slot >= T7_ROM_SLOT_COUNT) return false;
    char key[8];
    slot_key(slot, key);
    esp_err_t err = nvs_erase_key(s_nvs, key);
    if (err != ESP_OK && err != ESP_ERR_NVS_NOT_FOUND) return false;
    if (err == ESP_OK && nvs_commit(s_nvs) != ESP_OK) return false;
    s_present[slot] = false;
    s_hash[slot] = 0;
    err = esp_partition_erase_range(s_partition, slot_offset(slot), slot_span(slot));
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Erase ROM %u: %s", slot, esp_err_to_name(err));
        return false;
    }
    *writer = (t7_rom_writer_t) { .slot = slot, .hash = 2166136261u, .active = true };
    return true;
}

bool t7_rom_store_write(t7_rom_writer_t *writer, const void *data, size_t length)
{
    if (!writer || !writer->active || !data || length == 0 ||
        length > s_sizes[writer->slot] - writer->written) return false;
    esp_err_t err = esp_partition_write(s_partition,
                                        slot_offset(writer->slot) + writer->written,
                                        data, length);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Write ROM %u: %s", writer->slot, esp_err_to_name(err));
        writer->active = false;
        return false;
    }
    writer->hash = fnv_update(writer->hash, data, length);
    writer->written += length;
    return true;
}

bool t7_rom_store_finish(t7_rom_writer_t *writer)
{
    if (!writer || !writer->active || writer->written != s_sizes[writer->slot])
        return false;
    writer->active = false;
    size_t base = slot_offset(writer->slot);
    uint8_t header[T7_FIGURE_HEADER_SIZE];
    if (esp_partition_read(s_partition, base, header, sizeof(header)) != ESP_OK)
        return false;
    if (writer->slot == 0) {
        uint8_t vector[2];
        if (esp_partition_read(s_partition, base + 0x3ffc, vector, 2) != ESP_OK ||
            (vector[0] == 0 && vector[1] == 0) ||
            (vector[0] == 0xff && vector[1] == 0xff)) return false;
    } else if (t7_figure_rom_slot(header, sizeof(header), writer->written) !=
               writer->slot) {
        return false;
    }
    uint8_t buf[1024];
    uint32_t verified = 2166136261u;
    for (size_t offset = 0; offset < writer->written; offset += sizeof(buf)) {
        size_t n = writer->written - offset;
        if (n > sizeof(buf)) n = sizeof(buf);
        if (esp_partition_read(s_partition, base + offset, buf, n) != ESP_OK)
            return false;
        verified = fnv_update(verified, buf, n);
    }
    if (verified != writer->hash) return false;
    rom_meta_t meta = { ROM_META_MAGIC, (uint32_t)writer->written, verified };
    char key[8];
    slot_key(writer->slot, key);
    esp_err_t err = nvs_set_blob(s_nvs, key, &meta, sizeof(meta));
    if (err == ESP_OK) err = nvs_commit(s_nvs);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Commit ROM %u: %s", writer->slot, esp_err_to_name(err));
        return false;
    }
    s_present[writer->slot] = true;
    s_hash[writer->slot] = verified;
    ESP_LOGI(TAG, "ROM %u imported (%u bytes, FNV32 %08lx)", writer->slot,
             (unsigned)writer->written, (unsigned long)verified);
    return true;
}
