#pragma once
#include <stddef.h>
#include <stdint.h>

#define T7_FIGURE_HEADER_SIZE 22u
#define T7_FIGURE_SMALL_SIZE 349536u
#define T7_FIGURE_LARGE_SIZE 524288u

/* Return a Tama-Go figure slot (1..12), or 0 for an unknown/malformed dump.
 * The figure ID lives in the second AA 55 header at byte 20. */
uint8_t t7_figure_rom_slot(const uint8_t *header, size_t header_size,
                           size_t file_size);
