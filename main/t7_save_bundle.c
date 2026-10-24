/* Portable .tgsave v1 container: explicit little-endian fields, CRC-32. */
#include "t7_save_bundle.h"
#include <string.h>

static void put16(uint8_t *p, uint16_t value)
{
    p[0] = (uint8_t)value;
    p[1] = (uint8_t)(value >> 8);
}

static void put32(uint8_t *p, uint32_t value)
{
    for (int i = 0; i < 4; i++) p[i] = (uint8_t)(value >> (i * 8));
}

static uint16_t get16(const uint8_t *p)
{
    return (uint16_t)(p[0] | (uint16_t)p[1] << 8);
}

static uint32_t get32(const uint8_t *p)
{
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 |
           (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

static uint32_t checksum(const uint8_t *buffer, size_t size)
{
    uint32_t crc = 0xffffffffu;
    for (size_t i = 0; i < size; i++) {
        uint8_t byte = i >= 24 && i < 28 ? 0 : buffer[i];
        crc ^= byte;
        for (int bit = 0; bit < 8; bit++)
            crc = (crc >> 1) ^ ((crc & 1u) ? 0xedb88320u : 0);
    }
    return ~crc;
}

size_t t7_save_bundle_seal(uint8_t *buffer, size_t capacity, uint32_t rom_hash,
                           uint16_t page_mask, size_t session_size)
{
    size_t total = T7_SAVE_BUNDLE_HEADER + T7_SAVE_BUNDLE_EEPROM + session_size;
    if (!buffer || !rom_hash || session_size > T7_SAVE_BUNDLE_SESSION_MAX ||
        total > capacity) return 0;
    memcpy(buffer, "TGSV", 4);
    put16(buffer + 4, 1);
    put16(buffer + 6, 0);
    put32(buffer + 8, (uint32_t)total);
    put32(buffer + 12, rom_hash);
    put32(buffer + 16, (uint32_t)session_size);
    put16(buffer + 20, page_mask);
    put16(buffer + 22, 0);
    put32(buffer + 24, 0);
    put32(buffer + 24, checksum(buffer, total));
    return total;
}

bool t7_save_bundle_parse(const uint8_t *buffer, size_t size,
                          t7_save_bundle_view_t *view)
{
    if (!buffer || !view || size < T7_SAVE_BUNDLE_HEADER + T7_SAVE_BUNDLE_EEPROM ||
        size > T7_SAVE_BUNDLE_MAX || memcmp(buffer, "TGSV", 4) != 0 ||
        get16(buffer + 4) != 1 || get16(buffer + 6) != 0 ||
        get32(buffer + 8) != size || get16(buffer + 22) != 0 ||
        get32(buffer + 12) == 0 ||
        get32(buffer + 16) != size - T7_SAVE_BUNDLE_HEADER - T7_SAVE_BUNDLE_EEPROM ||
        get32(buffer + 24) != checksum(buffer, size)) return false;
    view->rom_hash = get32(buffer + 12);
    view->page_mask = get16(buffer + 20);
    view->eeprom = buffer + T7_SAVE_BUNDLE_HEADER;
    view->session = view->eeprom + T7_SAVE_BUNDLE_EEPROM;
    view->session_size = get32(buffer + 16);
    return true;
}
