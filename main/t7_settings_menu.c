#include "t7_settings_menu.h"
#include "t7_figures.h"
#include "t7_core/t7.h"
#include <string.h>

#define OPEN_HOLD_US 1500000LL
#define INSERT_DELAY_US 2500000LL

void settings_menu_init(settings_menu_t *menu, uint8_t volume,
                        uint8_t brightness, uint8_t figure,
                        uint16_t available_figures)
{
    memset(menu, 0, sizeof(*menu));
    menu->volume = volume <= 100 ? volume : SETTINGS_DEFAULT_VOLUME;
    menu->brightness = brightness >= SETTINGS_MIN_BRIGHTNESS && brightness <= 100
                       ? brightness : SETTINGS_DEFAULT_BRIGHTNESS;
    menu->figure = figure <= T7_FIGURE_COUNT &&
                   (figure == 0 || (available_figures & (1u << (figure - 1))))
                   ? figure : 0;
    menu->figure_cursor = menu->figure;
    menu->available_figures = available_figures;
}

uint8_t settings_menu_figure_neighbor(const settings_menu_t *menu,
                                      uint8_t cursor, bool forward)
{
    uint8_t next = cursor <= SETTINGS_FIGURE_BACK ? cursor : 0;
    do {
        next = forward ? (next == SETTINGS_FIGURE_BACK ? 0 : next + 1)
                       : (next == 0 ? SETTINGS_FIGURE_BACK : next - 1);
    } while (next > 0 && next <= T7_FIGURE_COUNT &&
             !(menu->available_figures & (1u << (next - 1))));
    return next;
}

static uint8_t adjust(uint8_t value, bool increase, uint8_t minimum)
{
    if (increase) return value > 100 - SETTINGS_STEP ? 100 : value + SETTINGS_STEP;
    return value < minimum + SETTINGS_STEP ? minimum : value - SETTINGS_STEP;
}

static settings_event_t choose_figure(settings_menu_t *menu, int64_t now_us)
{
    menu->page = SETTINGS_LIST;
    if (menu->figure_cursor == SETTINGS_FIGURE_TRANSFER) {
        menu->page = SETTINGS_CLOSED;
        return SETTINGS_TRANSFER_REQUESTED;
    }
    if (menu->figure_cursor == SETTINGS_FIGURE_BACK ||
        menu->figure_cursor == menu->figure) return SETTINGS_CHANGED;
    menu->figure = menu->figure_cursor;
    menu->pending_figure = menu->figure;
    menu->insert_at_us = menu->figure ? now_us + INSERT_DELAY_US : 0;
    return SETTINGS_FIGURE_SELECTED;
}

settings_event_t settings_menu_key(settings_menu_t *menu, uint8_t key,
                                   bool down, int64_t now_us)
{
    if (menu->wait_release_key) {
        if (key == menu->wait_release_key && !down)
            menu->wait_release_key = 0;
        return SETTINGS_NONE;
    }
    if (menu->page == SETTINGS_CLOSED) {
        if (key == T7_BTN_A) {
            if (down) {
                menu->up_down = true;
                menu->up_pressed_us = now_us;
            } else if (menu->up_down) {
                menu->up_down = false;
                if (now_us - menu->up_pressed_us >= OPEN_HOLD_US) {
                    menu->page = SETTINGS_LIST;
                    menu->item = 0;
                    return SETTINGS_OPENED;
                }
                return SETTINGS_GAME_UP_TAP;
            }
        }
        return SETTINGS_NONE;
    }
    if (menu->page == SETTINGS_FIGURE && key == T7_BTN_C && !down &&
        menu->ok_down) {
        menu->ok_down = false;
        if (now_us - menu->ok_pressed_us >= OPEN_HOLD_US) {
            menu->page = SETTINGS_LIST;
            return SETTINGS_CHANGED;
        }
        return choose_figure(menu, now_us);
    }
    if (!down) return SETTINGS_NONE;

    if (menu->page == SETTINGS_LIST) {
        if (key == T7_BTN_A || key == T7_BTN_B) {
            menu->item = key == T7_BTN_A
                         ? (menu->item == 0 ? SETTINGS_ITEM_COUNT - 1 : menu->item - 1)
                         : (menu->item + 1) % SETTINGS_ITEM_COUNT;
            return SETTINGS_CHANGED;
        }
        if (key == T7_BTN_C) {
            menu->wait_release_key = T7_BTN_C;
            if (menu->item == 0) menu->page = SETTINGS_VOLUME;
            else if (menu->item == 1) menu->page = SETTINGS_BRIGHTNESS;
            else if (menu->item == 2) {
                menu->figure_cursor = menu->figure;
                menu->page = SETTINGS_FIGURE;
            } else {
                menu->page = SETTINGS_CLOSED;
                return SETTINGS_CLOSED_EVENT;
            }
            return SETTINGS_CHANGED;
        }
    } else if (menu->page == SETTINGS_VOLUME ||
               menu->page == SETTINGS_BRIGHTNESS) {
        if (key == T7_BTN_A || key == T7_BTN_B) {
            bool volume = menu->page == SETTINGS_VOLUME;
            uint8_t *value = volume ? &menu->volume : &menu->brightness;
            uint8_t next = adjust(*value, key == T7_BTN_A,
                                  volume ? 0 : SETTINGS_MIN_BRIGHTNESS);
            if (*value == next) return SETTINGS_NONE;
            *value = next;
            return volume ? SETTINGS_VOLUME_CHANGED : SETTINGS_BRIGHTNESS_CHANGED;
        }
        if (key == T7_BTN_C) {
            bool volume = menu->page == SETTINGS_VOLUME;
            menu->page = SETTINGS_LIST;
            menu->wait_release_key = T7_BTN_C;
            return volume ? SETTINGS_VOLUME_SAVED : SETTINGS_BRIGHTNESS_SAVED;
        }
    } else if (menu->page == SETTINGS_FIGURE) {
        if (key == T7_BTN_A || key == T7_BTN_B) {
            menu->figure_cursor = settings_menu_figure_neighbor(
                menu, menu->figure_cursor, key == T7_BTN_B);
            return SETTINGS_CHANGED;
        }
        if (key == T7_BTN_C) {
            menu->ok_down = true;
            menu->ok_pressed_us = now_us;
            return SETTINGS_NONE;
        }
    }
    return SETTINGS_NONE;
}

settings_event_t settings_menu_tick(settings_menu_t *menu, int64_t now_us)
{
    if (menu->page == SETTINGS_CLOSED && menu->up_down &&
        now_us - menu->up_pressed_us >= OPEN_HOLD_US) {
        menu->page = SETTINGS_LIST;
        menu->item = 0;
        menu->up_down = false;
        menu->wait_release_key = T7_BTN_A;
        return SETTINGS_OPENED;
    }
    if (menu->page == SETTINGS_FIGURE && menu->ok_down &&
        now_us - menu->ok_pressed_us >= OPEN_HOLD_US) {
        menu->page = SETTINGS_LIST;
        menu->ok_down = false;
        menu->wait_release_key = T7_BTN_C;
        return SETTINGS_CHANGED;
    }
    if (menu->pending_figure && now_us >= menu->insert_at_us) {
        menu->pending_figure = 0;
        return SETTINGS_FIGURE_INSERT;
    }
    return SETTINGS_NONE;
}
