#include "t7_settings_menu.h"
#include "t7_figures.h"
#include "t7_core/t7.h"
#include <assert.h>
#include <stdio.h>

static void open_menu(settings_menu_t *menu)
{
    assert(settings_menu_key(menu, T7_BTN_A, true, 100) == SETTINGS_NONE);
    assert(settings_menu_tick(menu, 1500099) == SETTINGS_NONE);
    assert(settings_menu_tick(menu, 1500100) == SETTINGS_OPENED);
    assert(menu->page == SETTINGS_LIST && menu->item == 0);
    assert(settings_menu_key(menu, T7_BTN_A, true, 1500110) == SETTINGS_NONE);
    assert(settings_menu_key(menu, T7_BTN_A, false, 1500120) == SETTINGS_NONE);
}

int main(void)
{
    settings_menu_t menu;
    settings_menu_init(&menu, 40, 65, 7, 0x0fff);
    assert(menu.volume == 40 && menu.brightness == 65 &&
           menu.figure == 7);
    assert(settings_menu_key(&menu, T7_BTN_A, true, 0) == SETTINGS_NONE);
    assert(settings_menu_key(&menu, T7_BTN_A, false, 100000) == SETTINGS_GAME_UP_TAP);
    assert(settings_menu_tick(&menu, 2000000) == SETTINGS_NONE);
    assert(settings_menu_key(&menu, T7_BTN_C, true, 0) == SETTINGS_NONE);
    assert(settings_menu_tick(&menu, 2000000) == SETTINGS_NONE);
    assert(settings_menu_key(&menu, T7_BTN_C, false, 2000100) == SETTINGS_NONE);
    open_menu(&menu);

    assert(settings_menu_key(&menu, T7_BTN_C, true, 1500200) == SETTINGS_CHANGED);
    assert(menu.page == SETTINGS_VOLUME);
    assert(settings_menu_key(&menu, T7_BTN_A, true, 1500210) == SETTINGS_NONE);
    assert(settings_menu_key(&menu, T7_BTN_C, false, 1500220) == SETTINGS_NONE);
    assert(settings_menu_key(&menu, T7_BTN_A, true, 1500230) == SETTINGS_VOLUME_CHANGED);
    assert(menu.volume == 45);
    assert(settings_menu_key(&menu, T7_BTN_B, true, 1500240) == SETTINGS_VOLUME_CHANGED);
    assert(menu.volume == 40);
    assert(settings_menu_key(&menu, T7_BTN_C, true, 1500250) == SETTINGS_VOLUME_SAVED);
    assert(menu.page == SETTINGS_LIST);
    assert(settings_menu_key(&menu, T7_BTN_C, false, 1500260) == SETTINGS_NONE);

    assert(settings_menu_key(&menu, T7_BTN_B, true, 1500300) == SETTINGS_CHANGED);
    assert(menu.item == 1);
    assert(settings_menu_key(&menu, T7_BTN_C, true, 1500310) == SETTINGS_CHANGED);
    assert(menu.page == SETTINGS_BRIGHTNESS);
    assert(settings_menu_key(&menu, T7_BTN_C, false, 1500320) == SETTINGS_NONE);
    assert(settings_menu_key(&menu, T7_BTN_B, true, 1500330) == SETTINGS_BRIGHTNESS_CHANGED);
    assert(menu.brightness == 60);
    assert(settings_menu_key(&menu, T7_BTN_C, true, 1500340) == SETTINGS_BRIGHTNESS_SAVED);
    assert(menu.page == SETTINGS_LIST);
    assert(settings_menu_key(&menu, T7_BTN_C, false, 1500350) == SETTINGS_NONE);

    assert(settings_menu_key(&menu, T7_BTN_B, true, 1500400) == SETTINGS_CHANGED);
    assert(menu.item == 2);
    assert(settings_menu_key(&menu, T7_BTN_C, true, 1500410) == SETTINGS_CHANGED);
    assert(menu.page == SETTINGS_FIGURE && menu.figure_cursor == 7);
    assert(settings_menu_key(&menu, T7_BTN_C, false, 1500420) == SETTINGS_NONE);
    assert(settings_menu_key(&menu, T7_BTN_B, true, 1500490) == SETTINGS_CHANGED);
    assert(menu.figure_cursor == 8);
    assert(settings_menu_key(&menu, T7_BTN_C, true, 1500500) == SETTINGS_NONE);
    assert(settings_menu_key(&menu, T7_BTN_C, false, 1500510) == SETTINGS_FIGURE_SELECTED);
    assert(menu.page == SETTINGS_LIST && menu.figure == 8 && menu.pending_figure == 8);
    assert(settings_menu_tick(&menu, 4000509) == SETTINGS_NONE);
    assert(settings_menu_tick(&menu, 4000510) == SETTINGS_FIGURE_INSERT);
    assert(menu.pending_figure == 0);

    assert(settings_menu_key(&menu, T7_BTN_C, true, 4000460) == SETTINGS_CHANGED);
    assert(menu.page == SETTINGS_FIGURE);
    assert(settings_menu_key(&menu, T7_BTN_C, false, 4000461) == SETTINGS_NONE);
    menu.figure_cursor = SETTINGS_FIGURE_TRANSFER;
    assert(settings_menu_key(&menu, T7_BTN_C, true, 4000470) == SETTINGS_NONE);
    assert(settings_menu_key(&menu, T7_BTN_C, false, 4000480) == SETTINGS_TRANSFER_REQUESTED);
    assert(menu.page == SETTINGS_CLOSED && menu.figure == 8);

    settings_menu_init(&menu, 255, 0, 255, 0);
    assert(menu.volume == SETTINGS_DEFAULT_VOLUME);
    assert(menu.brightness == SETTINGS_DEFAULT_BRIGHTNESS);
    assert(menu.figure == 0);
    menu.page = SETTINGS_BRIGHTNESS;
    menu.brightness = SETTINGS_MIN_BRIGHTNESS;
    assert(settings_menu_key(&menu, T7_BTN_B, true, 0) == SETTINGS_NONE);
    menu.brightness = 100;
    assert(settings_menu_key(&menu, T7_BTN_A, true, 0) == SETTINGS_NONE);
    menu.page = SETTINGS_VOLUME;
    menu.volume = 0;
    assert(settings_menu_key(&menu, T7_BTN_B, true, 0) == SETTINGS_NONE);
    menu.volume = 100;
    assert(settings_menu_key(&menu, T7_BTN_A, true, 0) == SETTINGS_NONE);
    menu.page = SETTINGS_FIGURE;
    menu.figure_cursor = 0;
    assert(settings_menu_key(&menu, T7_BTN_A, true, 0) == SETTINGS_CHANGED);
    assert(menu.figure_cursor == SETTINGS_FIGURE_BACK);
    assert(settings_menu_key(&menu, T7_BTN_B, true, 0) == SETTINGS_CHANGED);
    assert(menu.figure_cursor == 0);
    assert(settings_menu_key(&menu, T7_BTN_B, true, 0) == SETTINGS_CHANGED);
    assert(menu.figure_cursor == SETTINGS_FIGURE_TRANSFER);
    assert(settings_menu_key(&menu, T7_BTN_B, true, 0) == SETTINGS_CHANGED);
    assert(menu.figure_cursor == SETTINGS_FIGURE_BACK);
    assert(settings_menu_key(&menu, T7_BTN_C, true, 0) == SETTINGS_NONE);
    assert(settings_menu_key(&menu, T7_BTN_C, false, 100) == SETTINGS_CHANGED);
    assert(menu.page == SETTINGS_LIST && menu.figure == 0);

    settings_menu_init(&menu, 40, 65, 7, 0x0041);
    assert(menu.figure == 7);
    assert(settings_menu_figure_neighbor(&menu, 0, true) == 1);
    assert(settings_menu_figure_neighbor(&menu, 1, true) == 7);
    assert(settings_menu_figure_neighbor(&menu, 7, true) == SETTINGS_FIGURE_TRANSFER);
    assert(settings_menu_figure_neighbor(&menu, SETTINGS_FIGURE_TRANSFER, true) == SETTINGS_FIGURE_BACK);
    assert(settings_menu_figure_neighbor(&menu, SETTINGS_FIGURE_BACK, false) == SETTINGS_FIGURE_TRANSFER);
    assert(settings_menu_figure_neighbor(&menu, SETTINGS_FIGURE_BACK, true) == 0);
    assert(settings_menu_figure_neighbor(&menu, 7, false) == 1);
    assert(settings_menu_figure_neighbor(&menu, 0, false) == SETTINGS_FIGURE_BACK);
    settings_menu_init(&menu, 40, 65, 7, 0x0001);
    assert(menu.figure == 0);
    menu.page = SETTINGS_FIGURE;
    assert(settings_menu_key(&menu, T7_BTN_B, true, 0) == SETTINGS_CHANGED);
    assert(menu.figure_cursor == 1);
    assert(settings_menu_key(&menu, T7_BTN_C, true, 0) == SETTINGS_NONE);
    assert(settings_menu_key(&menu, T7_BTN_C, false, 100) == SETTINGS_FIGURE_SELECTED);
    assert(menu.page == SETTINGS_LIST && menu.figure == 1);

    menu.page = SETTINGS_FIGURE;
    menu.figure_cursor = 0;
    assert(settings_menu_key(&menu, T7_BTN_C, true, 1000) == SETTINGS_NONE);
    assert(settings_menu_tick(&menu, 1500999) == SETTINGS_NONE);
    assert(settings_menu_tick(&menu, 1501000) == SETTINGS_CHANGED);
    assert(menu.page == SETTINGS_LIST && menu.figure == 1);
    assert(settings_menu_key(&menu, T7_BTN_C, false, 1501010) == SETTINGS_NONE);

    settings_menu_init(&menu, 40, 65, 0, 0);
    open_menu(&menu);
    assert(settings_menu_key(&menu, T7_BTN_A, true, 1500200) == SETTINGS_CHANGED);
    assert(menu.item == 3);
    assert(settings_menu_key(&menu, T7_BTN_C, true, 1500210) == SETTINGS_CLOSED_EVENT);
    assert(menu.page == SETTINGS_CLOSED);

    settings_menu_init(&menu, 40, 65, 0, 0);
    open_menu(&menu);
    assert(settings_menu_key(&menu, T7_BTN_B, true, 1500200) == SETTINGS_CHANGED);
    assert(settings_menu_key(&menu, T7_BTN_B, true, 1500210) == SETTINGS_CHANGED);
    assert(menu.item == 2);
    assert(settings_menu_key(&menu, T7_BTN_C, true, 1500220) == SETTINGS_CHANGED);
    assert(menu.page == SETTINGS_FIGURE && menu.figure_cursor == 0);
    assert(settings_menu_key(&menu, T7_BTN_C, false, 1500230) == SETTINGS_NONE);
    assert(settings_menu_key(&menu, T7_BTN_B, true, 1500240) == SETTINGS_CHANGED);
    assert(menu.figure_cursor == SETTINGS_FIGURE_TRANSFER);
    assert(settings_menu_key(&menu, T7_BTN_C, true, 1500250) == SETTINGS_NONE);
    assert(settings_menu_key(&menu, T7_BTN_C, false, 1500260) == SETTINGS_TRANSFER_REQUESTED);
    assert(menu.page == SETTINGS_CLOSED);

    settings_menu_init(&menu, 40, 65, 0, 0);
    assert(settings_menu_key(&menu, T7_BTN_A, true, 0) == SETTINGS_NONE);
    assert(settings_menu_key(&menu, T7_BTN_A, false, 1500000) == SETTINGS_OPENED);
    assert(menu.page == SETTINGS_LIST);

    puts("Settings UP hold, figures, nested transfer and exit: PASS");
    return 0;
}
