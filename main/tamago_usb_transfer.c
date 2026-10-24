/* Bounded, acknowledged ROM transfer over the ESP32-C3 USB Serial/JTAG port.
 * Commands are ASCII lines prefixed TG1; DATA is followed by exactly its
 * declared number of raw bytes. The host waits for an ACK before each chunk. */
#include "tamago_usb_transfer.h"
#include "t7_platform.h"
#include "t7_rom_store.h"
#include "t7_save_bundle.h"
#include "driver/usb_serial_jtag.h"
#include "driver/usb_serial_jtag_vfs.h"
#include "esp_log.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define USB_CHUNK_SIZE 1024u
#define USB_LINE_SIZE 80u
static uint8_t s_chunk[USB_CHUNK_SIZE];
static const char *TAG = "tama_usb";

static bool send_line(const char *line)
{
    size_t size = strlen(line);
    if (usb_serial_jtag_write_bytes(line, size, pdMS_TO_TICKS(1000)) != size)
        return false;
    return usb_serial_jtag_wait_tx_done(pdMS_TO_TICKS(1000)) == ESP_OK;
}

static bool send_bytes(const uint8_t *data, size_t size)
{
    while (size) {
        size_t count = size < 512 ? size : 512;
        if (usb_serial_jtag_write_bytes(data, count, pdMS_TO_TICKS(1000)) != count)
            return false;
        data += count;
        size -= count;
    }
    return usb_serial_jtag_wait_tx_done(pdMS_TO_TICKS(2000)) == ESP_OK;
}

static bool read_line(char out[USB_LINE_SIZE])
{
    size_t used = 0;
    for (;;) {
        uint8_t c;
        if (usb_serial_jtag_read_bytes(&c, 1, pdMS_TO_TICKS(1000)) != 1)
            return false;
        if (c == '\n') {
            out[used] = 0;
            return true;
        }
        if (c == '\r') continue;
        if (used + 1 >= USB_LINE_SIZE || c < 0x20 || c > 0x7e) {
            used = 0;
            continue;
        }
        out[used++] = (char)c;
    }
}

static bool read_exact(size_t count)
{
    size_t used = 0;
    int timeouts = 0;
    while (used < count) {
        int got = usb_serial_jtag_read_bytes(s_chunk + used, count - used,
                                             pdMS_TO_TICKS(1000));
        if (got > 0) {
            used += (size_t)got;
            timeouts = 0;
        } else if (++timeouts >= 10) {
            return false;
        }
    }
    return true;
}

static uint16_t ready_mask(void)
{
    return (uint16_t)((t7_rom_store_figure_mask() << 1) |
                      (t7_rom_store_has(0) ? 1u : 0u));
}

static void usb_task(void *arg)
{
    (void)arg;
    usb_serial_jtag_vfs_use_driver();
    esp_log_level_set("*", ESP_LOG_NONE); /* Keep protocol replies unambiguous. */
    t7_rom_writer_t writer = {0};
    bool uploading = false;
    uint8_t *save_data = NULL;
    size_t save_size = 0;
    size_t save_written = 0;
    char line[USB_LINE_SIZE];
    for (;;) {
        if (!read_line(line)) continue;
        if (strcmp(line, "TG1 HELLO") == 0) {
            char reply[40];
            snprintf(reply, sizeof(reply), "TG1 READY %04X\n", ready_mask());
            (void)send_line(reply);
            continue;
        }
        if (strcmp(line, "TG1 SAVE GET") == 0) {
            if (uploading || save_data) {
                (void)send_line("TG1 ERR BUSY\n");
                continue;
            }
            uint8_t *backup = malloc(T7_SAVE_BUNDLE_MAX);
            size_t backup_size = 0;
            if (!backup || !t7_platform_export_save(backup, T7_SAVE_BUNDLE_MAX,
                                                     &backup_size)) {
                free(backup);
                (void)send_line("TG1 ERR SAVE\n");
                continue;
            }
            char header[40];
            snprintf(header, sizeof(header), "TG1 SAVE %u\n", (unsigned)backup_size);
            (void)send_line(header);
            bool sent = send_bytes(backup, backup_size);
            free(backup);
            if (sent) (void)send_line("TG1 OK SAVE\n");
            continue;
        }
        unsigned save_length = 0;
        char extra;
        if (sscanf(line, "TG1 SAVE BEGIN %u %c", &save_length, &extra) == 1) {
            free(save_data);
            save_data = NULL;
            uploading = false;
            if (save_length < T7_SAVE_BUNDLE_HEADER + T7_SAVE_BUNDLE_EEPROM ||
                save_length > T7_SAVE_BUNDLE_MAX ||
                !(save_data = malloc(save_length))) {
                (void)send_line("TG1 ERR SIZE\n");
            } else {
                save_size = save_length;
                save_written = 0;
                (void)send_line("TG1 OK BEGIN\n");
            }
            continue;
        }
        unsigned slot = 0, size = 0;
        if (sscanf(line, "TG1 BEGIN %u %u %c", &slot, &size, &extra) == 2) {
            free(save_data);
            save_data = NULL;
            if (slot >= T7_ROM_SLOT_COUNT ||
                size != t7_rom_store_expected_size((uint8_t)slot)) {
                (void)send_line("TG1 ERR SIZE\n");
            } else if (!t7_rom_store_begin((uint8_t)slot, &writer)) {
                (void)send_line("TG1 ERR FLASH\n");
            } else {
                uploading = true;
                (void)send_line("TG1 OK BEGIN\n");
            }
            continue;
        }
        unsigned count = 0;
        if (sscanf(line, "TG1 DATA %u %c", &count, &extra) == 1) {
            size_t remaining = save_data ? save_size - save_written :
                               uploading ? t7_rom_store_expected_size(writer.slot) -
                                           writer.written : 0;
            if (count == 0 || count > USB_CHUNK_SIZE || count > remaining) {
                (void)send_line("TG1 ERR DATA\n");
                continue;
            }
            if (!read_exact(count)) {
                uploading = false;
                free(save_data);
                save_data = NULL;
                (void)send_line("TG1 ERR TIMEOUT\n");
                continue;
            }
            if (save_data) {
                memcpy(save_data + save_written, s_chunk, count);
                save_written += count;
            } else if (!t7_rom_store_write(&writer, s_chunk, count)) {
                uploading = false;
                (void)send_line("TG1 ERR FLASH\n");
                continue;
            }
            char reply[40];
            snprintf(reply, sizeof(reply), "TG1 ACK %u\n",
                     (unsigned)(save_data ? save_written : writer.written));
            (void)send_line(reply);
            continue;
        }
        if (strcmp(line, "TG1 END") == 0) {
            if (save_data) {
                t7_save_import_result_t result = save_written == save_size
                    ? t7_platform_import_save(save_data, save_size)
                    : T7_SAVE_IMPORT_FORMAT;
                free(save_data);
                save_data = NULL;
                (void)send_line(result == T7_SAVE_IMPORT_OK ? "TG1 OK END\n" :
                                result == T7_SAVE_IMPORT_ROM ? "TG1 ERR ROM\n" :
                                result == T7_SAVE_IMPORT_STORAGE ? "TG1 ERR NVS\n" :
                                "TG1 ERR VERIFY\n");
            } else if (!uploading || !t7_rom_store_finish(&writer)) {
                (void)send_line("TG1 ERR VERIFY\n");
            } else {
                (void)send_line("TG1 OK END\n");
            }
            uploading = false;
            continue;
        }
        if (strcmp(line, "TG1 FINISH") == 0) {
            if (!t7_rom_store_has(0)) {
                (void)send_line("TG1 ERR MAIN\n");
            } else if (!t7_platform_set_transfer_mode(T7_TRANSFER_GAME)) {
                (void)send_line("TG1 ERR NVS\n");
            } else {
                (void)send_line("TG1 OK FINISH\n");
                vTaskDelay(pdMS_TO_TICKS(500));
                esp_restart();
            }
            continue;
        }
        (void)send_line("TG1 ERR COMMAND\n");
    }
}

bool tamago_usb_transfer_start(void)
{
    usb_serial_jtag_driver_config_t config = {
        .rx_buffer_size = 2048, .tx_buffer_size = 1024,
    };
    esp_err_t err = usb_serial_jtag_driver_install(&config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "USB driver: %s", esp_err_to_name(err));
        return false;
    }
    if (xTaskCreate(usb_task, "tama_usb", 4096, NULL, 5, NULL) != pdPASS) {
        (void)usb_serial_jtag_driver_uninstall();
        return false;
    }
    return true;
}
