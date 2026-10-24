/* ROM-backed smoke test for the portable emulator. Build with
 * tools/test_tamago_core.sh; the private ROM is supplied locally. */
#include "t7.h"
#include "t7_internal.h"
#include "pebble_compat.h"
#include <assert.h>
#include <stddef.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint8_t *s_rom;
static size_t s_rom_size;
static uint8_t s_pages[16][256];
static bool s_present[16];

extern void figure_rom_set_active(const unsigned char *ptr, unsigned int len);

void tama_log(const char *fmt, ...) { (void)fmt; }
void t7_compat_set_rom_path(const char *path) { (void)path; }
void t7_compat_set_eeprom_path(const char *path) { (void)path; }
ResHandle resource_get_handle(int id) { return id == RESOURCE_ID_TAMA_ROM ? 1 : 0; }
size_t resource_size(ResHandle h) { return h == 1 ? s_rom_size : 0; }
size_t resource_load_byte_range(ResHandle h, size_t off, uint8_t *out, size_t n)
{
    if (h != 1 || off >= s_rom_size) return 0;
    if (n > s_rom_size - off) n = s_rom_size - off;
    memcpy(out, s_rom + off, n);
    return n;
}
const uint8_t *t7_compat_rom_mapped(void) { return s_rom; }
bool persist_exists(uint32_t key) { return key >= 100 && key < 116 && s_present[key - 100]; }
int persist_read_data(uint32_t key, void *out, size_t n)
{
    if (!persist_exists(key) || n < 256) return -1;
    memcpy(out, s_pages[key - 100], 256);
    return 256;
}
status_t persist_write_data(uint32_t key, const void *in, size_t n)
{
    if (key < 100 || key >= 116 || n != 256) return -1;
    memcpy(s_pages[key - 100], in, n);
    s_present[key - 100] = true;
    return S_SUCCESS;
}
status_t persist_delete(uint32_t key)
{
    if (key < 100 || key >= 116) return -1;
    s_present[key - 100] = false;
    return S_SUCCESS;
}
void persist_flush_to_disk(void) {}
int persist_page_count(void) { return 16; }
int persist_page_size(void) { return 256; }
int persist_backup_page(int page, uint8_t *out)
{
    return persist_read_data((uint32_t)page + 100, out, 256);
}
void persist_restore_page(int page, const uint8_t *in)
{
    (void)persist_write_data((uint32_t)page + 100, in, 256);
}

static void step_frame(void)
{
    /* Match the device's bounded CPU slices while testing a full ROM boot. */
    for (int slice = 0; slice < 4; slice++)
        assert(t7_step_cycles(50000) >= 50000);
}

static void test_figure_spi(const char *path, size_t expected_len)
{
    FILE *file = fopen(path, "rb");
    assert(file);
    uint8_t *data = malloc(expected_len);
    assert(data && fread(data, 1, expected_len, file) == expected_len);
    assert(fgetc(file) == EOF);
    fclose(file);
    assert(data[1] == 0xAA && data[2] == 0x55);

    figure_rom_set_active(data, (unsigned int)expected_len);
    t7_figure_set(1);
    assert(t7_figure_get() == 1);
    t7_io_write(0x3016, 0x10); /* CS high, then low to start a transaction. */
    t7_io_write(0x3016, 0x00);
    const uint8_t tx[] = { 0x03, 0x00, 0x00, 0x01, 0x00, 0x00 };
    for (size_t i = 0; i < sizeof(tx); i++) t7_io_write(0x30B3, tx[i]);
    for (int i = 0; i < 4; i++) assert(t7_io_read(0x30B6) == 0);
    assert(t7_io_read(0x30B6) == 0xAA);
    assert(t7_io_read(0x30B6) == 0x55);

    t7_io_write(0x3016, 0x10);
    t7_figure_set(0);
    figure_rom_set_active(NULL, 0);
    t7_io_write(0x3016, 0x00);
    for (size_t i = 0; i < sizeof(tx); i++) t7_io_write(0x30B3, tx[i]);
    for (int i = 0; i < 4; i++) assert(t7_io_read(0x30B6) == 0);
    assert(t7_io_read(0x30B6) == 0xFF);
    t7_io_write(0x3016, 0x10);
    free(data);
}

/* An optional, locally supplied session at the birthday setup screen lets
 * device diagnosis cover the full path to the first two care icons without
 * checking a user's private save into the repository. */
static void test_icon_navigation_from_session(void)
{
    const char *path = getenv("TAMAGO_BIRTHDAY_SESSION");
    if (!path || !*path) return;
    FILE *file = fopen(path, "rb");
    assert(file);
    uint8_t blob[2330];
    assert(fread(blob, 1, sizeof(blob), file) == sizeof(blob));
    assert(fgetc(file) == EOF);
    fclose(file);
    assert(t7_deserialize_state(blob + 8, sizeof(blob) - 8));
    t7_set_buttons(0);
    for (int i = 0; i < 10; i++) {
        t7_set_buttons(T7_BTN_B);
        for (int f = 0; f < 4; f++) step_frame();
        t7_set_buttons(0);
        for (int f = 0; f < 6; f++) step_frame();
    }
    for (int f = 0; f < 12000; f++) step_frame();
    t7_set_buttons(T7_BTN_B); /* acknowledge the birth announcement */
    for (int f = 0; f < 5; f++) step_frame();
    t7_set_buttons(0);
    for (int f = 0; f < 20; f++) step_frame();

    uint8_t icons[T7_ICON_COUNT];
    t7_set_buttons(T7_BTN_A);
    for (int f = 0; f < 6; f++) step_frame();
    t7_get_icons(icons);
    assert(icons[0] != 0 && icons[1] == 0);
    t7_set_buttons(0);
    for (int f = 0; f < 6; f++) step_frame();
    t7_set_buttons(T7_BTN_A);
    for (int f = 0; f < 6; f++) step_frame();
    t7_get_icons(icons);
    assert(icons[0] == 0 && icons[1] != 0);
    puts("Tama-Go saved-session A icon navigation: PASS");
}

int main(int argc, char **argv)
{
    assert(argc == 4);
    FILE *file = fopen(argv[1], "rb");
    assert(file);
    assert(fseek(file, 0, SEEK_END) == 0);
    long length = ftell(file);
    assert(length == 655360);
    assert(fseek(file, 0, SEEK_SET) == 0);
    s_rom_size = (size_t)length;
    s_rom = malloc(s_rom_size);
    assert(s_rom && fread(s_rom, 1, s_rom_size, file) == s_rom_size);
    fclose(file);

    assert(t7_init());
    assert(!t7_is_friends());
    for (int i = 0; i < 600; i++) step_frame(); /* 30 game seconds */
    uint8_t display[T7_LCD_WIDTH * T7_LCD_HEIGHT];
    t7_get_display(display);
    int ink = 0;
    for (size_t i = 0; i < sizeof(display); i++) ink += display[i] != display[0];
    assert(ink > 50); /* the real ROM reached the first interactive screen */
    uint8_t before[sizeof(display)];
    memcpy(before, display, sizeof(display));

    uint32_t size = t7_serialize_state(NULL, 0);
    assert(size > 0 && size < 4096);
    uint8_t *state = malloc(size);
    assert(state && t7_serialize_state(state, size) == size);
    const size_t p_offset = 8 + offsetof(cpu6502_t, p);
    const uint8_t live_p = t7_cpu_get_p();
    assert(state[4] == 2 && state[p_offset] == live_p);
    t7_cpu_set_p(FLAG_I | FLAG_U);
    assert(t7_deserialize_state(state, size));
    assert(t7_cpu_get_p() == live_p);
    state[4] = 1;
    state[p_offset] = FLAG_I | FLAG_U;
    assert(t7_deserialize_state(state, size));
    assert((t7_cpu_get_p() & FLAG_I) == 0); /* older sessions must not freeze */
    state[4] = 2;
    state[p_offset] = live_p;
    assert(t7_deserialize_state(state, size));
    t7_set_buttons(T7_BTN_A);
    for (int i = 0; i < 10; i++) step_frame();
    assert(t7_deserialize_state(state, size));
    t7_get_display(display);
    assert(memcmp(display, before, sizeof(display)) == 0);
    test_figure_spi(argv[2], 349536);
    test_figure_spi(argv[3], 524288);
    t7_set_buttons(T7_BTN_B);
    for (int i = 0; i < 5; i++) step_frame();
    t7_set_buttons(0);
    for (int i = 0; i < 40; i++) step_frame();
    t7_get_display(display);
    test_icon_navigation_from_session();
    t7_release();
    free(state);
    free(s_rom);
    puts("Tama-Go boot, display, session restore and figure SPI: PASS");
    return 0;
}
