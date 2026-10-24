#include "t7_figure_identify.h"

uint8_t t7_figure_rom_slot(const uint8_t *header, size_t header_size,
                           size_t file_size)
{
    if (!header || header_size < T7_FIGURE_HEADER_SIZE ||
        header[1] != 0xaa || header[2] != 0x55 ||
        header[17] != 0xaa || header[18] != 0x55 ||
        header[19] != 3 || header[21] != 3 ||
        header[20] < 3 || header[20] > 14) return 0;

    uint8_t slot = header[20] - 2;
    if (file_size != (slot <= 6 ? T7_FIGURE_SMALL_SIZE : T7_FIGURE_LARGE_SIZE) ||
        header[3] != (slot <= 6 ? 8 : 4)) return 0;
    return slot;
}
