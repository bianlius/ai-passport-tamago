#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define T7_SAVE_BUNDLE_HEADER 28u
#define T7_SAVE_BUNDLE_EEPROM 4096u
#define T7_SAVE_BUNDLE_SESSION_MAX 4096u
#define T7_SAVE_BUNDLE_MAX (T7_SAVE_BUNDLE_HEADER + T7_SAVE_BUNDLE_EEPROM + \
                            T7_SAVE_BUNDLE_SESSION_MAX)

typedef struct {
    uint32_t rom_hash;
    uint16_t page_mask;
    const uint8_t *eeprom;
    const uint8_t *session;
    size_t session_size;
} t7_save_bundle_view_t;

/* Payload starts at HEADER: 4096 EEPROM bytes followed by session bytes.
 * Seal writes the header and CRC after the payload has been filled. */
size_t t7_save_bundle_seal(uint8_t *buffer, size_t capacity, uint32_t rom_hash,
                           uint16_t page_mask, size_t session_size);
bool t7_save_bundle_parse(const uint8_t *buffer, size_t size,
                          t7_save_bundle_view_t *view);
