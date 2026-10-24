/* Names stay in firmware; the user supplies each Figure ROM through the portal. */
#include "t7_figures.h"
#include "t7_rom_store.h"
#include "t7_core/t7.h"

extern void figure_rom_set_active(const unsigned char *ptr, unsigned int len);

typedef struct {
    const char *name;
    const char *menu_name;
} figure_t;

static const figure_t s_figures[T7_FIGURE_COUNT] = {
    { "Mametchi", "麻每吉 Mametchi" },
    { "Memetchi", "美眉吉 Memetchi" },
    { "Kuchipatchi", "咕叽叭吉 Kuchipatchi" },
    { "Chamametchi", "美眉麻每吉 Chamametchi" },
    { "Violetchi", "紫罗兰吉 Violetchi" },
    { "Ringotchi", "苹果吉 Ringotchi" },
    { "Kuromametchi", "黑黑麻每吉 Kuromametchi" },
    { "Sebiretchi", "背鳍吉 Sebiretchi" },
    { "Ponytchi", "小马吉 Ponytchi" },
    { "Ichigotchi", "草莓吉 Ichigotchi" },
    { "Shimashimatchi", "条纹吉 Shimashimatchi" },
    { "Makiko", "麻纪子 Makiko" },
};

static bool s_mapped;
static esp_partition_mmap_handle_t s_handle;

const char *t7_figure_name(uint8_t index)
{
    return index == 0 ? "None" :
           index <= T7_FIGURE_COUNT ? s_figures[index - 1].name : "Invalid";
}

const char *t7_figure_menu_name(uint8_t index)
{
    return index == 0 ? "无配件" :
           index <= T7_FIGURE_COUNT ? s_figures[index - 1].menu_name : "未知配件";
}

bool t7_figure_available(uint8_t index)
{
    return index == 0 || (index <= T7_FIGURE_COUNT && t7_rom_store_has(index));
}

bool t7_figure_activate(uint8_t index)
{
    if (!t7_figure_available(index)) return false;
    figure_rom_set_active(NULL, 0);
    t7_figure_set(0);
    if (s_mapped) {
        esp_partition_munmap(s_handle);
        s_mapped = false;
    }
    if (index == 0) return true;
    const uint8_t *rom = NULL;
    if (!t7_rom_store_map(index, &rom, &s_handle)) return false;
    s_mapped = true;
    figure_rom_set_active(rom, (unsigned)t7_rom_store_expected_size(index));
    t7_figure_set(1);
    return true;
}
