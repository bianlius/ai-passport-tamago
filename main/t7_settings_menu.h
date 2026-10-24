#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "t7_figures.h"

#define SETTINGS_DEFAULT_VOLUME 40
#define SETTINGS_DEFAULT_BRIGHTNESS 65
#define SETTINGS_MIN_BRIGHTNESS 5
#define SETTINGS_STEP 5
#define SETTINGS_ITEM_COUNT 4
#define SETTINGS_FIGURE_TRANSFER (T7_FIGURE_COUNT + 1)
#define SETTINGS_FIGURE_BACK (T7_FIGURE_COUNT + 2)

typedef enum {
    SETTINGS_CLOSED,
    SETTINGS_LIST,
    SETTINGS_VOLUME,
    SETTINGS_BRIGHTNESS,
    SETTINGS_FIGURE,
} settings_page_t;

typedef enum {
    SETTINGS_NONE,
    SETTINGS_OPENED,
    SETTINGS_GAME_UP_TAP,
    SETTINGS_CHANGED,
    SETTINGS_VOLUME_CHANGED,
    SETTINGS_BRIGHTNESS_CHANGED,
    SETTINGS_VOLUME_SAVED,
    SETTINGS_BRIGHTNESS_SAVED,
    SETTINGS_FIGURE_SELECTED,
    SETTINGS_FIGURE_INSERT,
    SETTINGS_TRANSFER_REQUESTED,
    SETTINGS_CLOSED_EVENT,
} settings_event_t;

typedef struct {
    settings_page_t page;
    uint8_t item;
    uint8_t volume;
    uint8_t brightness;
    uint8_t figure;
    uint8_t figure_cursor;
    uint8_t pending_figure;
    uint16_t available_figures;
    bool ok_down;
    bool up_down;
    uint8_t wait_release_key;
    int64_t ok_pressed_us;
    int64_t up_pressed_us;
    int64_t insert_at_us;
} settings_menu_t;

void settings_menu_init(settings_menu_t *menu, uint8_t volume,
                        uint8_t brightness, uint8_t figure,
                        uint16_t available_figures);
settings_event_t settings_menu_key(settings_menu_t *menu, uint8_t key,
                                   bool down, int64_t now_us);
settings_event_t settings_menu_tick(settings_menu_t *menu, int64_t now_us);
uint8_t settings_menu_figure_neighbor(const settings_menu_t *menu,
                                      uint8_t cursor, bool forward);
