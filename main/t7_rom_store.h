#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "esp_partition.h"

#define T7_ROM_SLOT_COUNT 13
#define T7_MAIN_ROM_SIZE 655360u

typedef struct {
    uint8_t slot;
    size_t written;
    uint32_t hash;
    bool active;
} t7_rom_writer_t;

bool t7_rom_store_init(void);
size_t t7_rom_store_expected_size(uint8_t slot);
bool t7_rom_store_has(uint8_t slot);
uint32_t t7_rom_store_hash(uint8_t slot);
uint16_t t7_rom_store_figure_mask(void);
bool t7_rom_store_map(uint8_t slot, const uint8_t **ptr,
                      esp_partition_mmap_handle_t *handle);
bool t7_rom_store_begin(uint8_t slot, t7_rom_writer_t *writer);
bool t7_rom_store_write(t7_rom_writer_t *writer, const void *data, size_t length);
bool t7_rom_store_finish(t7_rom_writer_t *writer);
