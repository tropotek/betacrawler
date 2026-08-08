#pragma once
// Generic ESP32 WROOM-32 dev board (clone), no external modules wired up.
// Onboard WiFi radio only -- see hardware/wifi/wifi_esp32_driver.cpp, not
// the AT-command driver the STM32 boards use to talk to an external ESP-01.
//
// Selected at compile time by platformio.ini:
//   -D BOARD_HEADER='"boards/esp32_wroom32.h"'
//   -D FW_MCU_ESP32=1
//
// Flashed over the board's own USB-UART bridge with esptool, never an
// ST-Link -- see [env:esp32_wroom32] in platformio.ini.

#define BOARD_ID "esp32_wroom32"

// --- features ---------------------------------------------------------------
#define FEATURE_LED     1
#define FEATURE_BUTTON  0
#define FEATURE_ST7789_240X240 0
#define FEATURE_SERVO   0
#define FEATURE_ESC0    0
#define FEATURE_ESC1    0
#define FEATURE_RX      0
#define FEATURE_WIFI    1
// No STM32-style ROM DFU on this part; esptool over USB is the flash path
// instead. dfu.cpp's existing FEATURE_DFU-off stub (already exercised by
// any board with DFU off) covers this with no new code.
#define FEATURE_DFU     0

// This board's `system` module always reports temp=0.0C / vdd=0.000V on the
// Telemetry page: the classic WROOM-32's die temperature sensor is
// undocumented on original silicon and there is no measurable VDD rail the
// way the STM32 boards' VREFINT trick reads (fixed onboard 3.3V regulator).
// See system_esp32_driver.cpp. Not a sensor fault -- just not implemented on
// this MCU.

// --- pin map ----------------------------------------------------------------
// GPIO2 is the onboard LED on most WROOM-32 devkit clones (betacrawlered
// "LED" or "D2" next to it) -- verify against your specific board once
// flashed; cheaper clones sometimes omit it or use a different pin.
#define LED_PIN         2
#define LED_ACTIVE_LOW  0

// No WIFI_RX_PIN/WIFI_TX_PIN/WIFI_BAUD here: those belong to the STM32
// AT-UART driver only. wifi_esp32_driver.cpp drives the radio in-chip
// through WiFi.h and needs no UART pins.
