#include "t7_save_bundle.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

int main(void)
{
    uint8_t file[T7_SAVE_BUNDLE_MAX] = {0};
    for (size_t i = 0; i < T7_SAVE_BUNDLE_EEPROM; i++)
        file[T7_SAVE_BUNDLE_HEADER + i] = (uint8_t)i;
    const uint8_t state[] = { 'G', 'M', 'A', 'T', 2, 0, 0, 0, 42 };
    memcpy(file + T7_SAVE_BUNDLE_HEADER + T7_SAVE_BUNDLE_EEPROM,
           state, sizeof(state));
    size_t n = t7_save_bundle_seal(file, sizeof(file), 0x12345678, 0xa55a,
                                   sizeof(state));
    assert(n == T7_SAVE_BUNDLE_HEADER + T7_SAVE_BUNDLE_EEPROM + sizeof(state));
    t7_save_bundle_view_t view;
    assert(t7_save_bundle_parse(file, n, &view));
    assert(view.rom_hash == 0x12345678 && view.page_mask == 0xa55a);
    assert(view.session_size == sizeof(state));
    assert(memcmp(view.session, state, sizeof(state)) == 0);
    assert(!t7_save_bundle_parse(file, n - 1, &view));
    file[T7_SAVE_BUNDLE_HEADER + 100] ^= 1;
    assert(!t7_save_bundle_parse(file, n, &view));
    file[T7_SAVE_BUNDLE_HEADER + 100] ^= 1;
    file[4] = 2;
    assert(!t7_save_bundle_parse(file, n, &view));
    assert(!t7_save_bundle_seal(file, n - 1, 1, 0, sizeof(state)));
    puts("Tama-Go save bundle format, truncation, CRC and version: PASS");
    return 0;
}
