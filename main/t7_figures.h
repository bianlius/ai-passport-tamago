#pragma once
#include <stdbool.h>
#include <stdint.h>

#define T7_FIGURE_COUNT 12

const char *t7_figure_name(uint8_t index);
const char *t7_figure_menu_name(uint8_t index);
bool t7_figure_available(uint8_t index);
bool t7_figure_activate(uint8_t index);
