idf_component_register(
    SRCS "tamago_main.c" "t7_platform.c" "t7_figures.c" "t7_figure_identify.c" "t7_settings_menu.c"
         "t7_rom_store.c" "t7_save_bundle.c" "tamago_transfer.c" "tamago_dns_server.c"
         "tamago_usb_transfer.c"
         "../assets/fonts/tamago_menu_font_14.c"
         "t7_core/t7.c" "t7_core/t7_cpu.c" "t7_core/t7_memory.c"
         "t7_core/t7_eeprom.c" "t7_core/t7_sound.c" "t7_core/figure_data.c"
    INCLUDE_DIRS "." "t7_core"
    REQUIRES bsp nvs_flash esp_timer esp_partition esp_wifi esp_netif
             esp_event esp_http_server esp_driver_usb_serial_jtag lwip
    EMBED_TXTFILES "tamago_transfer.html"
)

target_compile_definitions(${COMPONENT_LIB} PRIVATE
    T7_NOMALLOC=1 T7_VIDEO_CYCLES=1 T7_FASTRUN=1
    T7_HOT= T7_SND_RING=2048 T7_SND_SYN_N=64 SPI_TRACE_MAX=16
)
