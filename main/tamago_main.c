/* Tama-Go on AI Passport: dedicated UI and real-time emulator worker. */
#include "bsp_audio.h"
#include "bsp_battery.h"
#include "bsp_button.h"
#include "bsp_display.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "esp_partition.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "lvgl.h"
#include "t7_core/t7.h"
#include "t7_core/t7_sound.h"
#include "t7_figures.h"
#include "t7_rom_store.h"
#include "t7_settings_menu.h"
#include "tamago_icons.h"
#include "tamago_transfer.h"
#include "tamago_usb_transfer.h"
#include "t7_platform.h"
#include <stdio.h>
#include <string.h>

LV_FONT_DECLARE(tamago_menu_font_14);
extern const uint32_t tamago_menu_codepoints[];

#define VIEW_SCALE 5
#define LCD_CELL_EDGE_BLEND 2
#define VIEW_W (T7_LCD_WIDTH * VIEW_SCALE)
#define VIEW_H (T7_LCD_HEIGHT * VIEW_SCALE)
#define ICON_SIZE TAMAGO_ICON_SIZE
#define FRAME_MS 50
#define EMU_SLICES 1
#define PERF_WINDOW_FRAMES 200
#define AUDIO_HZ 16000
#define AUDIO_SAMPLES (AUDIO_HZ * FRAME_MS / 1000)
#define MAX_DIRTY_SPANS 8

static const char *TAG = "tamago";
static QueueHandle_t s_key_queue;
static QueueHandle_t s_audio_queue;
static lv_obj_t *s_canvas;
static lv_obj_t *s_icons[T7_ICON_COUNT];
static lv_obj_t *s_battery;
static lv_obj_t *s_settings_panel;
static lv_obj_t *s_settings_title;
static lv_obj_t *s_settings_rows[SETTINGS_ITEM_COUNT];
static lv_obj_t *s_settings_value;
static lv_obj_t *s_settings_bar;
static lv_obj_t *s_settings_footer;
static lv_obj_t *s_transfer_status;
static lv_obj_t *s_transfer_qr;
static lv_obj_t *s_transfer_qr_label;
static lv_obj_t *s_transfer_choice_rows[3];
static lv_obj_t *s_transfer_choice_hint;
static char s_transfer_wifi_qr[80];
static uint8_t s_transfer_mode;
static uint8_t s_transfer_choice;
static uint8_t s_transfer_choice_count;
static esp_partition_mmap_handle_t s_main_rom_handle;
static uint8_t s_volume_percent = SETTINGS_DEFAULT_VOLUME;
static uint8_t s_brightness_percent = SETTINGS_DEFAULT_BRIGHTNESS;
static uint16_t *s_canvas_buf;
static uint16_t (*s_icon_buf)[ICON_SIZE * ICON_SIZE];
static uint8_t s_pixels[T7_LCD_WIDTH * T7_LCD_HEIGHT];
static uint8_t s_pixels_drawn[T7_LCD_WIDTH * T7_LCD_HEIGHT];
static uint8_t s_icon_values[T7_ICON_COUNT];
static uint8_t s_icon_drawn[T7_ICON_COUNT];

typedef struct { uint8_t key; uint8_t down; } key_event_t;
typedef struct { uint16_t samples; int16_t pcm[AUDIO_SAMPLES]; } audio_block_t;

static uint8_t button_mask(bsp_btn_t btn)
{
    if (btn == BSP_BTN_UP) return T7_BTN_A;
    if (btn == BSP_BTN_DOWN) return T7_BTN_B;
    return T7_BTN_C;
}

static void on_button(bsp_btn_t btn, bsp_btn_ev_t ev, void *user)
{
    (void)user;
    if (ev != BSP_BTN_PRESS && ev != BSP_BTN_RELEASE) return;
    key_event_t event = { .key = button_mask(btn), .down = ev == BSP_BTN_PRESS };
    (void)xQueueSend(s_key_queue, &event, 0);
}


static void style_label(lv_obj_t *obj, uint32_t color, const lv_font_t *font)
{
    lv_obj_set_style_text_color(obj, lv_color_hex(color), 0);
    lv_obj_set_style_text_font(obj, font, 0);
}

static lv_obj_t *label_at(lv_obj_t *parent, const char *text, int x, int y,
                          uint32_t color, const lv_font_t *font)
{
    lv_obj_t *obj = lv_label_create(parent);
    lv_label_set_text(obj, text);
    lv_obj_set_pos(obj, x, y);
    style_label(obj, color, font);
    return obj;
}

static void build_ui(void)
{
    lv_obj_t *screen = lv_screen_active();
    lv_obj_set_style_bg_color(screen, lv_color_hex(0x17212B), 0);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);
    lv_obj_remove_flag(screen, LV_OBJ_FLAG_SCROLLABLE);

    s_battery = label_at(screen, "", 282, 10, 0xBBD7C6, &lv_font_montserrat_14);

    lv_obj_t *frame = lv_obj_create(screen);
    lv_obj_set_pos(frame, 34, 37);
    lv_obj_set_size(frame, 252, 167);
    lv_obj_set_style_radius(frame, 9, 0);
    lv_obj_set_style_bg_color(frame, lv_color_hex(0xDDE2D5), 0);
    lv_obj_set_style_border_color(frame, lv_color_hex(0x789183), 0);
    lv_obj_set_style_border_width(frame, 4, 0);
    lv_obj_set_style_pad_all(frame, 0, 0);
    lv_obj_remove_flag(frame, LV_OBJ_FLAG_SCROLLABLE);

    s_canvas = lv_canvas_create(screen);
    lv_canvas_set_buffer(s_canvas, s_canvas_buf, VIEW_W, VIEW_H, LV_COLOR_FORMAT_RGB565);
    lv_obj_set_pos(s_canvas, 40, 43);
    lv_obj_remove_flag(s_canvas, LV_OBJ_FLAG_SCROLLABLE);
    memset(s_pixels_drawn, 0xFF, sizeof(s_pixels_drawn));

    for (int i = 0; i < T7_ICON_COUNT; i++) {
        s_icons[i] = lv_canvas_create(screen);
        lv_canvas_set_buffer(s_icons[i], s_icon_buf[i], ICON_SIZE, ICON_SIZE,
                             LV_COLOR_FORMAT_RGB565);
        lv_obj_set_pos(s_icons[i], 48 + (i % 5) * 50, i < 5 ? 7 : 207);
        lv_obj_remove_flag(s_icons[i], LV_OBJ_FLAG_SCROLLABLE);
    }
    memset(s_icon_drawn, 0xFF, sizeof(s_icon_drawn));

    s_settings_panel = lv_obj_create(screen);
    lv_obj_set_pos(s_settings_panel, 22, 35);
    lv_obj_set_size(s_settings_panel, 276, 150);
    lv_obj_set_style_radius(s_settings_panel, 9, 0);
    lv_obj_set_style_bg_color(s_settings_panel, lv_color_hex(0x17212B), 0);
    lv_obj_set_style_bg_opa(s_settings_panel, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(s_settings_panel, lv_color_hex(0xD7DFB1), 0);
    lv_obj_set_style_border_width(s_settings_panel, 2, 0);
    lv_obj_set_style_pad_all(s_settings_panel, 0, 0);
    lv_obj_remove_flag(s_settings_panel, LV_OBJ_FLAG_SCROLLABLE);
    s_settings_title = label_at(s_settings_panel, "设置", 0, 0,
                                0xF2D376, &tamago_menu_font_14);
    lv_obj_align(s_settings_title, LV_ALIGN_TOP_MID, 0, 9);
    for (int i = 0; i < SETTINGS_ITEM_COUNT; i++)
        s_settings_rows[i] = label_at(s_settings_panel, "", 14, 31 + i * 23,
                                      0x96A998, &tamago_menu_font_14);
    s_settings_value = label_at(s_settings_panel, "", 0, 0,
                                0xF2D376, &lv_font_montserrat_20);
    lv_obj_align(s_settings_value, LV_ALIGN_TOP_MID, 0, 59);
    s_settings_bar = lv_bar_create(s_settings_panel);
    lv_obj_set_pos(s_settings_bar, 38, 101);
    lv_obj_set_size(s_settings_bar, 200, 12);
    lv_bar_set_range(s_settings_bar, 0, 100);
    lv_obj_set_style_radius(s_settings_bar, 4, 0);
    lv_obj_set_style_bg_color(s_settings_bar, lv_color_hex(0x43545B), LV_PART_MAIN);
    lv_obj_set_style_bg_color(s_settings_bar, lv_color_hex(0xF2D376), LV_PART_INDICATOR);
    s_settings_footer = label_at(s_settings_panel, "", 34, 124,
                                 0xD7DFB1, &tamago_menu_font_14);
    lv_obj_add_flag(s_settings_panel, LV_OBJ_FLAG_HIDDEN);
}

static void render_transfer_choice(void)
{
    for (uint8_t i = 0; i < s_transfer_choice_count; ++i) {
        lv_obj_set_style_bg_color(s_transfer_choice_rows[i],
                                  lv_color_hex(i == s_transfer_choice ? 0xF2D376 : 0x263943), 0);
        lv_obj_set_style_text_color(s_transfer_choice_rows[i],
                                    lv_color_hex(i == s_transfer_choice ? 0x17212B : 0xE6EDDB), 0);
    }
    lv_label_set_text(s_transfer_choice_hint,
                      s_transfer_choice == 0 ? "无线：常用浏览器即可" :
                      s_transfer_choice == 1 ? "有线：电脑 Chrome/Edge" : "");
}

static void build_transfer_choice_ui(void)
{
    lv_obj_t *screen = lv_screen_active();
    bool has_game_rom = t7_rom_store_has(0);
    lv_obj_set_style_bg_color(screen, lv_color_hex(0x17212B), 0);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);
    lv_obj_remove_flag(screen, LV_OBJ_FLAG_SCROLLABLE);
    label_at(screen, has_game_rom ? "选择传输方式" : "未检测到游戏 ROM",
             24, 18, 0xF2D376,
             &tamago_menu_font_14);
    if (!has_game_rom)
        label_at(screen, "请选择传输方式导入", 24, 43, 0xBBD7C6,
                 &tamago_menu_font_14);
    const char *items[] = {"无线传输", "USB 有线传输", "开始游戏"};
    s_transfer_choice_count = has_game_rom ? 3 : 2;
    s_transfer_choice = 0;
    for (uint8_t i = 0; i < s_transfer_choice_count; ++i) {
        lv_obj_t *row = label_at(screen, items[i], 24,
                                 (has_game_rom ? 57 : 76) + 46 * i,
                                 0xE6EDDB, &tamago_menu_font_14);
        lv_obj_set_size(row, 272, 36);
        lv_obj_set_style_radius(row, 6, 0);
        lv_obj_set_style_pad_left(row, 12, 0);
        lv_obj_set_style_pad_top(row, 8, 0);
        lv_obj_set_style_bg_opa(row, LV_OPA_COVER, 0);
        s_transfer_choice_rows[i] = row;
    }
    s_transfer_choice_hint = label_at(screen, "",
                                      24, has_game_rom ? 189 : 174,
                                      0xBBD7C6, &tamago_menu_font_14);
    render_transfer_choice();
    label_at(screen, "UP/DOWN 选择   OK 确认", 24, 218, 0xBBD7C6,
             &tamago_menu_font_14);
}

static void build_transfer_ui(bool online, const char *ssid, const char *password)
{
    lv_obj_t *screen = lv_screen_active();
    lv_obj_set_style_bg_color(screen, lv_color_hex(0x17212B), 0);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);
    lv_obj_remove_flag(screen, LV_OBJ_FLAG_SCROLLABLE);
    label_at(screen, "导入 Tama-Go 文件", 14, 10, 0xF2D376,
             &tamago_menu_font_14);
    if (!online) {
        s_transfer_status = label_at(screen, "热点启动失败，请重启设备", 17, 105,
                                     0xF2D376, &tamago_menu_font_14);
        return;
    }
    snprintf(s_transfer_wifi_qr, sizeof(s_transfer_wifi_qr),
             "WIFI:T:WPA;S:%s;P:%s;;", ssid, password);
    s_transfer_qr = lv_qrcode_create(screen);
    lv_qrcode_set_size(s_transfer_qr, 150);
    lv_qrcode_set_light_color(s_transfer_qr, lv_color_hex(0xE6EDDB));
    lv_qrcode_set_dark_color(s_transfer_qr, lv_color_hex(0x17212B));
    lv_obj_set_pos(s_transfer_qr, 11, 46);
    if (lv_qrcode_update(s_transfer_qr, s_transfer_wifi_qr,
                         strlen(s_transfer_wifi_qr)) != LV_RESULT_OK)
        ESP_LOGE(TAG, "Wi-Fi QR generation failed");
    s_transfer_qr_label = label_at(screen, "扫码连接热点", 170, 49, 0xF2D376,
                                   &tamago_menu_font_14);
    label_at(screen, ssid, 170, 73, 0xE6EDDB, &lv_font_montserrat_14);
    label_at(screen, "常用浏览器均可", 170, 94, 0xBBD7C6,
             &tamago_menu_font_14);
    label_at(screen, "热点密码", 170, 111, 0xBBD7C6,
             &tamago_menu_font_14);
    label_at(screen, password, 170, 133, 0xE6EDDB, &lv_font_montserrat_14);
    label_at(screen, "未弹出网页:", 170, 165, 0xBBD7C6,
             &tamago_menu_font_14);
    label_at(screen, "192.168.4.1", 170, 184, 0xF2D376,
             &lv_font_montserrat_14);
    s_transfer_status = label_at(screen, "UP 网页码  DOWN 选择  OK 游戏", 15, 215,
                                 0xBBD7C6, &tamago_menu_font_14);
}

static void build_usb_ui(bool online)
{
    lv_obj_t *screen = lv_screen_active();
    lv_obj_set_style_bg_color(screen, lv_color_hex(0x17212B), 0);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);
    lv_obj_remove_flag(screen, LV_OBJ_FLAG_SCROLLABLE);
    label_at(screen, "USB 有线传输", 25, 22, 0xF2D376,
             &tamago_menu_font_14);
    label_at(screen, "用数据线连接电脑", 25, 68, 0xE6EDDB,
             &tamago_menu_font_14);
    label_at(screen, "在电脑浏览器打开本地网页", 25, 96, 0xE6EDDB,
             &tamago_menu_font_14);
    label_at(screen, "TamaGo-USB.html", 25, 124, 0xF2D376,
             &lv_font_montserrat_14);
    label_at(screen, "Chrome / Edge", 25, 148, 0xBBD7C6,
             &lv_font_montserrat_14);
    label_at(screen, "UP 选择方式  OK 启动游戏", 25, 178, 0xBBD7C6,
             &tamago_menu_font_14);
    s_transfer_status = label_at(screen,
                                 online ? "等待电脑连接" : "USB 启动失败，请重启",
                                 25, 210, 0xF2D376, &tamago_menu_font_14);
}

static void transfer_task(void *arg)
{
    (void)arg;
    key_event_t event;
    bool web_qr = false;
    for (;;) {
        if (xQueueReceive(s_key_queue, &event, portMAX_DELAY) != pdTRUE ||
            !event.down) continue;
        if (s_transfer_mode == T7_TRANSFER_CHOICE) {
            if (event.key == T7_BTN_A || event.key == T7_BTN_B) {
                s_transfer_choice = (uint8_t)((s_transfer_choice +
                    (event.key == T7_BTN_A ? s_transfer_choice_count - 1 : 1)) %
                    s_transfer_choice_count);
                if (bsp_lvgl_lock(100)) {
                    render_transfer_choice();
                    bsp_lvgl_unlock();
                }
            } else if (event.key == T7_BTN_C) {
                if (s_transfer_choice == 2) {
                    if (t7_platform_set_transfer_mode(T7_TRANSFER_GAME)) esp_restart();
                    continue;
                }
                s_transfer_mode = s_transfer_choice == 0 ? T7_TRANSFER_WIFI : T7_TRANSFER_USB;
                bool online;
                char ssid[32] = {0};
                char password[16] = {0};
                if (s_transfer_mode == T7_TRANSFER_USB)
                    online = tamago_usb_transfer_start();
                else
                    online = tamago_transfer_start(ssid, sizeof(ssid),
                                                   password, sizeof(password));
                if (bsp_lvgl_lock(1000)) {
                    lv_obj_clean(lv_screen_active());
                    if (s_transfer_mode == T7_TRANSFER_USB) build_usb_ui(online);
                    else build_transfer_ui(online, ssid, password);
                    bsp_lvgl_unlock();
                }
            }
            continue;
        }
        if (s_transfer_mode == T7_TRANSFER_USB && event.key == T7_BTN_A) {
            if (t7_platform_set_transfer_mode(T7_TRANSFER_CHOICE)) esp_restart();
            continue;
        }
        if (s_transfer_mode == T7_TRANSFER_WIFI && event.key == T7_BTN_B) {
            if (t7_platform_set_transfer_mode(T7_TRANSFER_CHOICE)) esp_restart();
            continue;
        }
        if (s_transfer_mode == T7_TRANSFER_WIFI && event.key == T7_BTN_A &&
            s_transfer_qr && bsp_lvgl_lock(100)) {
            web_qr = !web_qr;
            const char *code = web_qr ? "http://192.168.4.1/" : s_transfer_wifi_qr;
            if (lv_qrcode_update(s_transfer_qr, code, strlen(code)) != LV_RESULT_OK)
                ESP_LOGE(TAG, "Transfer QR update failed");
            lv_label_set_text(s_transfer_qr_label,
                              web_qr ? "扫码打开网页" : "扫码连接热点");
            bsp_lvgl_unlock();
            continue;
        }
        if (event.key != T7_BTN_C) continue;
        if (t7_rom_store_has(0) &&
            t7_platform_set_transfer_mode(T7_TRANSFER_GAME))
            esp_restart();
        if (s_transfer_status && bsp_lvgl_lock(100)) {
            lv_label_set_text(s_transfer_status, "请先导入游戏 ROM");
            bsp_lvgl_unlock();
        }
    }
}

static bool menu_font_complete(void)
{
    for (const uint32_t *cp = tamago_menu_codepoints; *cp; ++cp) {
        lv_font_glyph_dsc_t dsc = {0};
        if (!lv_font_get_glyph_dsc(&tamago_menu_font_14, &dsc, *cp, 0) ||
            dsc.is_placeholder) {
            ESP_LOGE(TAG, "Menu font missing U+%04lx", (unsigned long)*cp);
            return false;
        }
    }
    return true;
}

static void render_figure_row(lv_obj_t *row, uint8_t figure, bool selected)
{
    if (figure == SETTINGS_FIGURE_BACK)
        lv_label_set_text_fmt(row, "%c 返回设置", selected ? '>' : ' ');
    else if (figure == SETTINGS_FIGURE_TRANSFER)
        lv_label_set_text_fmt(row, "%c 文件传输", selected ? '>' : ' ');
    else
        lv_label_set_text_fmt(row, "%c %02u  %s", selected ? '>' : ' ',
                              figure, t7_figure_menu_name(figure));
}

static void render_settings_menu(const settings_menu_t *menu)
{
    if (menu->page == SETTINGS_CLOSED) {
        lv_obj_add_flag(s_settings_panel, LV_OBJ_FLAG_HIDDEN);
        return;
    }
    bool editing = menu->page == SETTINGS_VOLUME ||
                   menu->page == SETTINGS_BRIGHTNESS;
    if (editing) {
        bool volume = menu->page == SETTINGS_VOLUME;
        uint8_t value = volume ? menu->volume : menu->brightness;
        lv_label_set_text(s_settings_title, volume ? "音量" : "亮度");
        lv_label_set_text_fmt(s_settings_value, "%u%%", value);
        lv_bar_set_range(s_settings_bar, 0, 100);
        lv_bar_set_value(s_settings_bar, value, LV_ANIM_OFF);
        lv_obj_remove_flag(s_settings_value, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(s_settings_bar, LV_OBJ_FLAG_HIDDEN);
        lv_label_set_text(s_settings_footer, "上/下 调整   确认键 保存");
    } else {
        lv_obj_add_flag(s_settings_value, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(s_settings_bar, LV_OBJ_FLAG_HIDDEN);
        if (menu->page == SETTINGS_LIST) {
            lv_label_set_text(s_settings_title, "设置");
            lv_label_set_text_fmt(s_settings_rows[0], "%c 音量       %u%%",
                                  menu->item == 0 ? '>' : ' ', menu->volume);
            lv_label_set_text_fmt(s_settings_rows[1], "%c 亮度       %u%%",
                                  menu->item == 1 ? '>' : ' ', menu->brightness);
            lv_label_set_text_fmt(s_settings_rows[2], "%c 人物配件",
                                  menu->item == 2 ? '>' : ' ');
            lv_label_set_text_fmt(s_settings_rows[3], "%c 返回游戏",
                                  menu->item == 3 ? '>' : ' ');
            lv_label_set_text(s_settings_footer, "上/下 选择   确认键 进入");
        } else {
            uint8_t prev = settings_menu_figure_neighbor(menu, menu->figure_cursor,
                                                         false);
            uint8_t next = settings_menu_figure_neighbor(menu, menu->figure_cursor,
                                                         true);
            lv_label_set_text(s_settings_title, "人物配件");
            if (prev == next) lv_label_set_text(s_settings_rows[0], "");
            else render_figure_row(s_settings_rows[0], prev, false);
            render_figure_row(s_settings_rows[1], menu->figure_cursor, true);
            render_figure_row(s_settings_rows[2], next, false);
            lv_label_set_text(s_settings_footer, menu->available_figures
                              ? "OK 进入   长按返回" : "未导入配件   长按返回");
        }
    }
    for (int i = 0; i < SETTINGS_ITEM_COUNT; i++) {
        bool show = !editing && (menu->page == SETTINGS_LIST || i < 3);
        if (show) {
            lv_obj_set_pos(s_settings_rows[i], 14,
                           menu->page == SETTINGS_FIGURE ? 40 + i * 27 : 31 + i * 23);
            lv_obj_remove_flag(s_settings_rows[i], LV_OBJ_FLAG_HIDDEN);
            bool selected = menu->page == SETTINGS_LIST ? i == menu->item : i == 1;
            lv_obj_set_style_text_color(s_settings_rows[i],
                                        lv_color_hex(selected ? 0xF2D376 : 0x96A998), 0);
        } else {
            lv_obj_add_flag(s_settings_rows[i], LV_OBJ_FLAG_HIDDEN);
        }
    }
    lv_obj_remove_flag(s_settings_panel, LV_OBJ_FLAG_HIDDEN);
}

static uint16_t blend_rgb565(uint16_t background, uint16_t ink, uint8_t alpha)
{
    unsigned inverse = 15u - alpha;
    unsigned r = (((background >> 11) & 31u) * inverse +
                  ((ink >> 11) & 31u) * alpha + 7u) / 15u;
    unsigned g = (((background >> 5) & 63u) * inverse +
                  ((ink >> 5) & 63u) * alpha + 7u) / 15u;
    unsigned b = ((background & 31u) * inverse +
                  (ink & 31u) * alpha + 7u) / 15u;
    return (uint16_t)((r << 11) | (g << 5) | b);
}

static void render_icon(int index, uint8_t value)
{
    static const uint32_t ink_rgb[4] = { 0x829596, 0xA8B9AB, 0xD6C78B, 0xF2D376 };
    uint16_t background = lv_color_to_u16(lv_color_hex(0x17212B));
    uint16_t ink = lv_color_to_u16(lv_color_hex(ink_rgb[value & 3]));
    uint16_t shades[16];
    for (int alpha = 0; alpha < 16; alpha++)
        shades[alpha] = blend_rgb565(background, ink, alpha);
    for (int y = 0; y < ICON_SIZE; y++) {
        for (int x = 0; x < ICON_SIZE; x++) {
            int pixel = y * ICON_SIZE + x;
            uint8_t pair = tamago_icon_alpha[index][pixel / 2];
            uint8_t alpha = (pixel & 1) ? pair & 15u : pair >> 4;
            s_icon_buf[index][pixel] = shades[alpha];
        }
    }
    lv_obj_invalidate(s_icons[index]);
    s_icon_drawn[index] = value;
}

static void render_game(void)
{
    /* Soft gray-white LCD, without the yellow-green cast of the old palette. */
    static const uint16_t shades[4] = { 0xE75B, 0xBE15, 0x7C0E, 0x31C6 };
    t7_get_display(s_pixels);
    t7_get_icons(s_icon_values);
    int8_t column_top[T7_LCD_WIDTH];
    int8_t column_bottom[T7_LCD_WIDTH];
    for (int x = 0; x < T7_LCD_WIDTH; x++) {
        column_top[x] = T7_LCD_HEIGHT;
        column_bottom[x] = -1;
    }
    int min_x = T7_LCD_WIDTH;
    int min_y = T7_LCD_HEIGHT;
    int max_x = -1;
    int max_y = -1;
    for (int y = 0; y < T7_LCD_HEIGHT; y++) {
        for (int x = 0; x < T7_LCD_WIDTH; x++) {
            int index = y * T7_LCD_WIDTH + x;
            uint8_t pixel = s_pixels[index];
            if (s_pixels_drawn[index] == pixel) continue;
            s_pixels_drawn[index] = pixel;
            if (x < min_x) min_x = x;
            if (x > max_x) max_x = x;
            if (y < min_y) min_y = y;
            if (y > max_y) max_y = y;
            if (y < column_top[x]) column_top[x] = y;
            if (y > column_bottom[x]) column_bottom[x] = y;
            uint16_t c = shades[pixel & 3];
            uint16_t edge = blend_rgb565(c, shades[0], LCD_CELL_EDGE_BLEND);
            int dst_x = x * VIEW_SCALE;
            int dst_y = y * VIEW_SCALE;
            for (int dy = 0; dy < VIEW_SCALE; dy++) {
                uint16_t *dst = &s_canvas_buf[(dst_y + dy) * VIEW_W + dst_x];
                for (int dx = 0; dx < VIEW_SCALE; dx++) {
                    dst[dx] = (dx == VIEW_SCALE - 1 || dy == VIEW_SCALE - 1)
                              ? edge : c;
                }
            }
        }
    }
    if (max_x >= 0) {
        lv_area_t coords;
        lv_obj_get_coords(s_canvas, &coords);
        lv_area_t spans[MAX_DIRTY_SPANS];
        int span_count = 0;
        for (int x = min_x; x <= max_x;) {
            if (column_bottom[x] < 0) { x++; continue; }
            int left = x;
            int top = T7_LCD_HEIGHT;
            int bottom = -1;
            do {
                if (column_top[x] < top) top = column_top[x];
                if (column_bottom[x] > bottom) bottom = column_bottom[x];
                x++;
            } while (x <= max_x && column_bottom[x] >= 0);
            if (span_count == MAX_DIRTY_SPANS) {
                span_count = -1;
                break;
            }
            spans[span_count++] = (lv_area_t) {
                .x1 = coords.x1 + left * VIEW_SCALE,
                .y1 = coords.y1 + top * VIEW_SCALE,
                .x2 = coords.x1 + x * VIEW_SCALE - 1,
                .y2 = coords.y1 + (bottom + 1) * VIEW_SCALE - 1,
            };
        }
        if (span_count < 0) {
            lv_area_t dirty = {
                .x1 = coords.x1 + min_x * VIEW_SCALE,
                .y1 = coords.y1 + min_y * VIEW_SCALE,
                .x2 = coords.x1 + (max_x + 1) * VIEW_SCALE - 1,
                .y2 = coords.y1 + (max_y + 1) * VIEW_SCALE - 1,
            };
            lv_obj_invalidate_area(s_canvas, &dirty);
        } else {
            for (int i = 0; i < span_count; i++)
                lv_obj_invalidate_area(s_canvas, &spans[i]);
        }
    }
    for (int i = 0; i < T7_ICON_COUNT; i++) {
        if (s_icon_drawn[i] != s_icon_values[i])
            render_icon(i, s_icon_values[i]);
    }
}

static void audio_task(void *arg)
{
    (void)arg;
    audio_block_t block;
    for (;;) {
        if (xQueueReceive(s_audio_queue, &block, portMAX_DELAY) == pdTRUE &&
            bsp_audio_write(block.pcm, block.samples * sizeof(block.pcm[0])) != ESP_OK)
            ESP_LOGW(TAG, "Audio output failed");
    }
}

static void game_task(void *arg)
{
    (void)arg;
    if (!t7_init()) {
        ESP_LOGE(TAG, "T7 core failed to start");
        vTaskDelete(NULL);
        return;
    }
    ESP_LOGI(TAG, "Session %s", t7_platform_load_state() ? "restored" : "new");
    settings_menu_t menu;
    settings_menu_init(&menu, s_volume_percent, s_brightness_percent,
                       t7_platform_load_figure(), t7_rom_store_figure_mask());
    if (!t7_figure_activate(menu.figure)) {
        ESP_LOGE(TAG, "Saved figure unavailable; starting without one");
        menu.figure = 0;
        menu.figure_cursor = 0;
        (void)t7_figure_activate(0);
    }
    ESP_LOGI(TAG, "Figure: %s", t7_figure_name(menu.figure));
    t7_snd_config(T7_CLOCK_RATE, AUDIO_HZ);
    t7_snd_set_mode(1);
    t7_snd_synth(1);
    t7_snd_capture(s_audio_queue != NULL);
    t7_snd_volume(45);

    uint8_t held = 0;
    bool menu_dirty = false;
    uint32_t frame = 0;
    int64_t perf_start_us = esp_timer_get_time();
    int64_t perf_emu_us = 0;
    int last_adc_bucket = -2;
    TickType_t next = xTaskGetTickCount();
    for (;;) {
        if ((frame & 1u) == 0) {
            int mv = bsp_button_last_mv();
            int bucket = mv < 0 ? -1 : mv < 150 ? 0 : mv < 447 ? 1 :
                         mv < 1900 ? 2 : 3;
            if (bucket != last_adc_bucket) {
                ESP_LOGI(TAG, "ADC mv=%d bucket=%d", mv, bucket);
                last_adc_bucket = bucket;
            }
        }
        key_event_t event;
        bool key_trace = false;
        bool pulse_up = false;
        /* Consume one edge per emulated frame. A quick tap can queue both
         * edges before this task runs; draining both here hides the press
         * from the Tama-Go firmware entirely. */
        if (xQueueReceive(s_key_queue, &event, 0) == pdTRUE) {
            bool captured = menu.page != SETTINGS_CLOSED ||
                            menu.wait_release_key || event.key == T7_BTN_A;
            ESP_LOGI(TAG, "KEY key=%u down=%u menu=%u", event.key, event.down, captured);
            settings_event_t action = settings_menu_key(&menu, event.key,
                                                         event.down, esp_timer_get_time());
            pulse_up = action == SETTINGS_GAME_UP_TAP;
            key_trace = !captured || pulse_up;
            if (captured) {
                if (action != SETTINGS_NONE && action != SETTINGS_GAME_UP_TAP)
                    menu_dirty = true;
                if (action == SETTINGS_VOLUME_CHANGED) {
                    s_volume_percent = menu.volume;
                    bsp_audio_set_volume(menu.volume);
                } else if (action == SETTINGS_BRIGHTNESS_CHANGED) {
                    s_brightness_percent = menu.brightness;
                    bsp_display_backlight(menu.brightness);
                } else if (action == SETTINGS_VOLUME_SAVED) {
                    if (!t7_platform_save_volume(menu.volume))
                        ESP_LOGW(TAG, "Volume will not survive reboot");
                } else if (action == SETTINGS_BRIGHTNESS_SAVED) {
                    if (!t7_platform_save_brightness(menu.brightness))
                        ESP_LOGW(TAG, "Brightness will not survive reboot");
                } else if (action == SETTINGS_FIGURE_SELECTED) {
                    (void)t7_figure_activate(0);
                    if (!t7_platform_save_figure(menu.figure))
                        ESP_LOGW(TAG, "Figure choice will not survive reboot");
                    ESP_LOGI(TAG, "Figure selected: %s", t7_figure_name(menu.figure));
                } else if (action == SETTINGS_TRANSFER_REQUESTED) {
                    if (t7_platform_save_state() &&
                        t7_platform_set_transfer_mode(T7_TRANSFER_CHOICE)) esp_restart();
                    ESP_LOGE(TAG, "Could not enter transfer mode");
                }
                if (menu.page != SETTINGS_CLOSED || menu.wait_release_key)
                    held = 0;
            } else {
                if (event.down) held |= event.key;
                else held &= (uint8_t)~event.key;
            }
        }
        settings_event_t tick_action = settings_menu_tick(&menu, esp_timer_get_time());
        if (tick_action == SETTINGS_OPENED) {
            held = 0;
            menu_dirty = true;
        } else if (tick_action == SETTINGS_FIGURE_INSERT) {
            if (!t7_figure_activate(menu.figure))
                ESP_LOGE(TAG, "Figure insertion failed: %s", t7_figure_name(menu.figure));
            else ESP_LOGI(TAG, "Figure inserted: %s", t7_figure_name(menu.figure));
        }
        t7_set_buttons(held | (pulse_up ? T7_BTN_A : 0));
        int64_t start_us = esp_timer_get_time();
        const uint32_t frame_cycles = T7_CLOCK_RATE * FRAME_MS / 1000u;
        for (int slice = 0; slice < EMU_SLICES; slice++) {
            uint32_t cycles = frame_cycles / EMU_SLICES;
            if (slice == EMU_SLICES - 1) cycles += frame_cycles % EMU_SLICES;
            t7_step_cycles(cycles);
            if (slice != EMU_SLICES - 1) vTaskDelay(1);
        }
        int64_t run_us = esp_timer_get_time() - start_us;
        perf_emu_us += run_us;
        if (key_trace) {
            uint8_t icons[T7_ICON_COUNT];
            uint16_t pc = 0;
            t7_get_icons(icons);
            t7_debug_get_state(&pc, NULL, NULL, NULL, NULL, NULL);
            ESP_LOGI(TAG, "KEY result held=%u pc=%04x icons=%u%u%u%u%u%u%u%u%u%u",
                     held | (pulse_up ? T7_BTN_A : 0), pc,
                     icons[0], icons[1], icons[2], icons[3], icons[4],
                     icons[5], icons[6], icons[7], icons[8], icons[9]);
        }
        TickType_t frame_ticks = pdMS_TO_TICKS(FRAME_MS);
        if (run_us > (int64_t)frame_ticks * portTICK_PERIOD_MS * 1000 && frame % 20 == 0)
            ESP_LOGW(TAG, "Emulation behind target speed: %lld us/frame",
                     (long long)run_us);

        if (s_audio_queue) {
            audio_block_t block;
            block.samples = AUDIO_SAMPLES;
            (void)t7_snd_pull(block.pcm, block.samples, block.samples * 2);
            (void)xQueueSend(s_audio_queue, &block, 0);
        }
        if (bsp_lvgl_lock(10)) {
            render_game();
            if (menu_dirty) {
                render_settings_menu(&menu);
                menu_dirty = false;
            }
            bsp_lvgl_unlock();
        }
        if (frame % (1000 / FRAME_MS * 30) == 0) {
            int soc = bsp_battery_soc();
            if (bsp_lvgl_lock(10)) {
                if (soc >= 0) lv_label_set_text_fmt(s_battery, "%d%%", soc);
                else lv_label_set_text(s_battery, "");
                bsp_lvgl_unlock();
            }
        }
        if (frame && frame % (1000 / FRAME_MS * 60) == 0 && !t7_platform_save_state())
            ESP_LOGW(TAG, "Session save failed");
        frame++;
        if (frame % PERF_WINDOW_FRAMES == 0) {
            int64_t now_us = esp_timer_get_time();
            ESP_LOGI(TAG, "Frame timing: wall=%lld emu=%lld us/frame stack=%lu heap=%lu largest=%lu",
                     (long long)((now_us - perf_start_us) / PERF_WINDOW_FRAMES),
                     (long long)(perf_emu_us / PERF_WINDOW_FRAMES),
                     (unsigned long)uxTaskGetStackHighWaterMark(NULL),
                     (unsigned long)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
                     (unsigned long)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL));
            perf_start_us = now_us;
            perf_emu_us = 0;
        }
        if (xTaskGetTickCount() - next >= frame_ticks) {
            /* Keep the original deadline after a brief overrun so later
             * frames can catch up. Drop it only if a full frame was missed. */
            next += frame_ticks;
            if (xTaskGetTickCount() - next >= frame_ticks)
                next = xTaskGetTickCount();
            /* A slow frame must still let the single-core idle task run. */
            vTaskDelay(1);
        } else {
            vTaskDelayUntil(&next, frame_ticks);
        }
    }
}

void app_main(void)
{
    s_key_queue = xQueueCreate(16, sizeof(key_event_t));
    if (!s_key_queue) { ESP_LOGE(TAG, "Key queue allocation failed"); return; }
    if (bsp_display_init() != ESP_OK) {
        ESP_LOGE(TAG, "Display initialization failed");
        return;
    }
    lv_display_t *display = bsp_lvgl_init();
    if (!display) { ESP_LOGE(TAG, "LVGL initialization failed"); return; }
    bsp_display_backlight(65);
    if (!bsp_lvgl_lock(1000)) { ESP_LOGE(TAG, "LVGL lock failed"); return; }
    if (!menu_font_complete()) { bsp_lvgl_unlock(); return; }
    lv_display_set_rotation(display, LV_DISPLAY_ROTATION_270);
    if (lv_display_get_horizontal_resolution(display) != 320 ||
        lv_display_get_vertical_resolution(display) != 240) {
        ESP_LOGE(TAG, "Landscape rotation failed");
        bsp_lvgl_unlock();
        return;
    }
    bsp_lvgl_unlock();
    if (!t7_platform_init()) return;
    if (!t7_rom_store_init()) return;
    s_volume_percent = t7_platform_load_volume();
    s_brightness_percent = t7_platform_load_brightness();
    bsp_display_backlight(s_brightness_percent);

    s_transfer_mode = t7_platform_transfer_mode();
    if (s_transfer_mode != T7_TRANSFER_GAME || !t7_rom_store_has(0)) {
        if (s_transfer_mode != T7_TRANSFER_CHOICE &&
            !t7_platform_set_transfer_mode(T7_TRANSFER_CHOICE))
            ESP_LOGW(TAG, "Transfer selection will not survive reboot");
        s_transfer_mode = T7_TRANSFER_CHOICE;
        if (bsp_lvgl_lock(1000)) {
            build_transfer_choice_ui();
            bsp_lvgl_unlock();
        }
        if (bsp_button_init(on_button, NULL) == ESP_OK &&
            xTaskCreate(transfer_task, "tama_transfer", 4096, NULL, 5, NULL) != pdPASS)
            ESP_LOGE(TAG, "Transfer key task allocation failed");
        return;
    }
    const uint8_t *rom = NULL;
    if (!t7_rom_store_map(0, &rom, &s_main_rom_handle)) {
        ESP_LOGE(TAG, "Main ROM mapping failed");
        return;
    }
    t7_platform_set_rom(rom);
    ESP_LOGI(TAG, "Main ROM FNV32 %08lx", (unsigned long)t7_platform_rom_hash());
    s_canvas_buf = heap_caps_calloc(VIEW_W * VIEW_H, sizeof(*s_canvas_buf),
                                    MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    s_icon_buf = heap_caps_calloc(T7_ICON_COUNT, sizeof(*s_icon_buf),
                                  MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (!s_canvas_buf || !s_icon_buf) {
        ESP_LOGE(TAG, "Game canvas allocation failed");
        return;
    }
    if (!bsp_lvgl_lock(1000)) return;
    build_ui();
    bsp_lvgl_unlock();

    (void)bsp_battery_init();
    if (bsp_audio_init() == ESP_OK &&
        bsp_audio_set_format(AUDIO_HZ, 16, 1) == ESP_OK) {
        bsp_audio_set_volume(s_volume_percent);
        s_audio_queue = xQueueCreate(3, sizeof(audio_block_t));
        if (s_audio_queue && xTaskCreate(audio_task, "t7_audio", 3072, NULL, 5, NULL) != pdPASS) {
            vQueueDelete(s_audio_queue);
            s_audio_queue = NULL;
        }
    } else {
        ESP_LOGW(TAG, "Audio unavailable; continuing silently");
    }
    if (bsp_button_init(on_button, NULL) != ESP_OK) {
        ESP_LOGE(TAG, "Button initialization failed");
        return;
    }
    ESP_LOGI(TAG, "Heap before game task: free=%lu largest=%lu",
             (unsigned long)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
             (unsigned long)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL));
    if (xTaskCreate(game_task, "t7_game", 6144, NULL, 6, NULL) != pdPASS)
        ESP_LOGE(TAG, "Emulator task allocation failed");
}
