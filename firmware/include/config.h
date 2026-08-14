#pragma once
// Project-level firmware configuration.
//
// This is the one file to edit when forking betacrawler into a real project:
// name, version, serial speed and the capacity limits below. Which *hardware*
// exists is not decided here -- that lives in the board header, included at
// the bottom.

// --- project identity -------------------------------------------------------
#define FW_PROJECT_NAME "betacrawler"
// betacrawler is a template. It stays at 1.0.0 forever; a fork bumps this. The
// app (backend + web UI) is versioned separately in app/web/app.js -- the two
// are unrelated projects and their numbers are not meant to track each other.
#define FW_VERSION      "1.0.0"

// --- link -------------------------------------------------------------------
// Must match `monitor_speed` in platformio.ini. There is no way to share one
// value between a C header and an ini file, so they are kept in sync by hand;
// a mismatch shows up immediately as garbage in the serial monitor.
#define FW_SERIAL_BAUD  115200

// --- capacity limits --------------------------------------------------------
// Static ceilings for the module registry. Everything is fixed-size: no
// malloc anywhere in this firmware. Raising these costs RAM
// (FW_MAX_PARAMS * sizeof(core::Value) is the big one, 36 bytes per slot),
// which is cheap on a 128KB part. Registry::add() refuses to exceed them
// rather than overflowing, and a native test covers that path.
// blackpill_f411ce ships device, system, button, led, rx, tank_drive, esc0
// and esc1 today -- 8 modules, exactly at the cap, zero headroom left.
// Turning on servo, st7789_240x240, or WiFi ALONGSIDE this board's mixed-tank
// build would need FW_MAX_MODULES raised first -- Registry::add() silently
// refuses the module that doesn't fit rather than overflowing, and a native
// test covers that path, but nothing today surfaces the refusal to a
// person, so don't rely on it as a warning.
#define FW_MAX_MODULES  8
#define FW_MAX_PARAMS   32
// This board's current build (led, button, esc0, esc1, rx enabled; servo and
// st7789_240x240 off) exposes 33 telemetry fields: rx alone publishes 16
// channels plus 7 link readings, esc0 and esc1 add 2 each (its pulse width
// and arm state), and the rest split across system/button. 40 rather than a
// bare fit leaves a little headroom for the next module or field;
// TlmValue is 4 bytes, so the headroom costs 32 bytes of RAM in one place --
// but there are two FW_MAX_TLM-sized arrays, not one: main.cpp's
// `static TlmValue g_tlm[FW_MAX_TLM]` (+32 bytes static) and
// st7789_240x240_driver.cpp's `core::TlmValue vals[FW_MAX_TLM]` (+32 more,
// on the display-refresh stack). So raising this headroom costs 32 bytes of
// static RAM plus 32 more on the display-refresh stack.
#define FW_MAX_TLM      40

// --- board ------------------------------------------------------------------
// BOARD_HEADER is supplied by platformio.ini per environment, e.g.
//   -D BOARD_HEADER='"boards/blackpill_f411ce.h"'
// so adding a board is a new header plus a new [env:] block, with no edit to
// any source file. See boards/_template.h.
#ifndef BOARD_HEADER
#error "BOARD_HEADER is not defined -- see build_flags in platformio.ini"
#endif
#include BOARD_HEADER

// --- feature defaults -------------------------------------------------------
// Every FEATURE_* symbol gets a 0 default AFTER the board header, so `#if
// FEATURE_X` is always legal even for a feature this board has never heard
// of. That is what lets a module's own headers guard themselves without
// every board header having to list every feature in existence.
//
// Convention: features are `#define FEATURE_X 1` / `0` and tested with a
// plain `#if`. No Marlin-style ENABLED()/DISABLED() macro machinery -- the
// defaults below buy the same "undefined is off" safety with none of the
// preprocessor gymnastics.
#ifndef FEATURE_LED
#define FEATURE_LED 0
#endif
#ifndef FEATURE_BUTTON
#define FEATURE_BUTTON 0
#endif
#ifndef FEATURE_ST7789_240X240
#define FEATURE_ST7789_240X240 0
#endif
#ifndef FEATURE_SERVO
#define FEATURE_SERVO 0
#endif
#ifndef FEATURE_ESC0
#define FEATURE_ESC0 0
#endif
#ifndef FEATURE_ESC1
#define FEATURE_ESC1 0
#endif
#ifndef FEATURE_TANK_DRIVE
#define FEATURE_TANK_DRIVE 0
#endif
#ifndef FEATURE_DFU
#define FEATURE_DFU 0
#endif
#ifndef FEATURE_RX
#define FEATURE_RX 0
#endif
#ifndef FEATURE_WIFI
#define FEATURE_WIFI 0
#endif

// Set only by an ESP32 environment's own build_flags (see platformio.ini's
// [env:esp32_wroom32]) to select the ESP32-native bodies of storage.cpp,
// hardware/system/system_driver.cpp and hardware/wifi/wifi_driver.cpp --
// each guards its own STM32-specific body with `#if !FW_MCU_ESP32` and is
// paired with a `*_esp32_*` file guarded the other way, so every board
// compiles cleanly with no per-environment build_src_filter bookkeeping.
// FW_TARGET_ARDUINO alone still answers "is this a real target at all,"
// exactly as it always has -- this only disambiguates which real target.
#ifndef FW_MCU_ESP32
#define FW_MCU_ESP32 0
#endif
