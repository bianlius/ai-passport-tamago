#include "t7_figure_identify.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

int main(void)
{
    for (uint8_t slot = 1; slot <= 12; slot++) {
        uint8_t header[T7_FIGURE_HEADER_SIZE] = {0};
        size_t size = slot <= 6 ? T7_FIGURE_SMALL_SIZE : T7_FIGURE_LARGE_SIZE;
        header[1] = header[17] = 0xaa;
        header[2] = header[18] = 0x55;
        header[3] = slot <= 6 ? 8 : 4;
        header[19] = header[21] = 3;
        header[20] = slot + 2;
        assert(t7_figure_rom_slot(header, sizeof(header), size) == slot);
        assert(t7_figure_rom_slot(header, sizeof(header) - 1, size) == 0);
        assert(t7_figure_rom_slot(header, sizeof(header), size - 1) == 0);
        header[18] = 0;
        assert(t7_figure_rom_slot(header, sizeof(header), size) == 0);
    }
    assert(t7_figure_rom_slot(NULL, T7_FIGURE_HEADER_SIZE,
                              T7_FIGURE_SMALL_SIZE) == 0);
    puts("Tama-Go figure header ID, size and rejection: PASS");
    return 0;
}
