#pragma once
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

typedef enum {
    T7_SAVE_IMPORT_OK,
    T7_SAVE_IMPORT_FORMAT,
    T7_SAVE_IMPORT_ROM,
    T7_SAVE_IMPORT_STORAGE,
} t7_save_import_result_t;

bool t7_platform_init(void);
void t7_platform_set_rom(const uint8_t *rom);
uint32_t t7_platform_rom_hash(void);
bool t7_platform_load_state(void);
bool t7_platform_save_state(void);
bool t7_platform_export_save(uint8_t *out, size_t capacity, size_t *written);
t7_save_import_result_t t7_platform_import_save(const uint8_t *data, size_t size);
uint8_t t7_platform_load_figure(void);
bool t7_platform_save_figure(uint8_t index);
uint8_t t7_platform_load_volume(void);
uint8_t t7_platform_load_brightness(void);
bool t7_platform_save_volume(uint8_t percent);
bool t7_platform_save_brightness(uint8_t percent);
#define T7_TRANSFER_GAME 0u
#define T7_TRANSFER_WIFI 1u
#define T7_TRANSFER_USB 2u
#define T7_TRANSFER_CHOICE 3u
uint8_t t7_platform_transfer_mode(void);
bool t7_platform_set_transfer_mode(uint8_t mode);
