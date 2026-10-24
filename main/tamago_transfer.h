#pragma once
#include <stdbool.h>
#include <stddef.h>

/* Starts a local WPA2 hotspot, captive portal and ROM upload server. */
bool tamago_transfer_start(char *ssid, size_t ssid_size,
                           char *password, size_t password_size);
