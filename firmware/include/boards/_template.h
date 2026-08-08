#pragma once
// Board header template -- copy to boards/<your-board>.h and edit.
//
// Adding a board is two steps and touches no source file:
//
//   1. Copy this file to boards/<your-board>.h and fill it in.
//   2. Add an environment to platformio.ini:
//
//        [env:<your-board>]
//        platform     = ststm32          ; or espressif32, ...
//        board        = <pio board id>
//        framework    = arduino
//        monitor_speed = 115200          ; must match FW_SERIAL_BAUD
//        build_flags  =
//            -Wswitch -Iinclude
//            -D FW_TARGET_ARDUINO=1
//            -D BOARD_HEADER='"boards/<your-board>.h"'
//            ; Required if FEATURE_RX is 1 on this board: the Arduino
//            ; default RX ring (64 bytes) tears on nearly every frame at
//            ; CRSF's ~150fps. rx_driver.cpp #errors at compile time if
//            ; this is missing or too small -- see its SERIAL_RX_BUFFER_SIZE
//            ; guard.
//            -D SERIAL_RX_BUFFER_SIZE=256
//        lib_deps     = bblanchon/ArduinoJson@^7.0.4
//
// A non-STM32 MCU family needs more than swapping `platform`. `[env:esp32_wroom32]`
// in platformio.ini plus `boards/esp32_wroom32.h` is a worked example -- follow that
// pair, not this STM32-only template, when the family differs. Three things it had
// to solve that this template doesn't cover:
//   (a) Any shared file with an MCU-specific body (storage.cpp, system_driver.cpp,
//       wifi_driver.cpp, ...) needs an ESP32-style twin (storage_esp32.cpp, etc.),
//       each guarded by a new `FW_MCU_<FAMILY>` macro, if the new MCU has its own
//       peripheral APIs the existing body can't reuse.
//   (b) If the platform's default C++ standard is older than the rest of the tree
//       (espressif32 defaults to gnu++11), add `-std=gnu++17` to build_flags AND
//       `build_unflags = -std=gnu++11` -- the framework appends its own -std after
//       build_flags, so the flag alone is silently overridden without the unflag.
//   (c) `extra_scripts = pre:scripts/config_hash.py` -- already required for every
//       board (editing a board header doesn't trigger a rebuild without it, since
//       `#include BOARD_HEADER` is invisible to SCons), just easier to forget while
//       chasing (a) and (b).
//
// Turning a feature on here is all that is needed to compile its module in:
// src/modules.cpp already has the matching #if block, and the parameters,
// telemetry fields and UI controls the module declares appear automatically
// (firmware schema -> backend -> web form). Turning one off removes its code,
// its parameters and its UI with no other edit.

// Reported by the `hello` op and shown in the app.
#define BOARD_ID "my-board"

// --- features ---------------------------------------------------------------
// Any FEATURE_* left out here defaults to 0 in config.h, so listing only what
// the board actually has is fine. Listing them explicitly (with 0) is clearer
// when the board *could* support something that is deliberately off.
#define FEATURE_LED     1
#define FEATURE_BUTTON  0
#define FEATURE_ST7789_240X240 0
#define FEATURE_SERVO   0
// Reboot-to-bootloader, so the app can flash this board over USB without a
// jumper. Requires DFU_SYSMEM_ADDR below. Only enable it on a part that has a
// USB DFU bootloader in ROM -- every STM32F4 does; check the reference manual
// for anything else.
#define FEATURE_DFU     0

// Required when FEATURE_DFU is 1: the base of system memory, where this
// part's ROM bootloader lives. Family-specific -- 0x1FFF0000 on an F411,
// different on an F103 or an H7. Look up the "system memory" row in the
// device's reference manual; a wrong value here means the jump lands nowhere
// and the board simply reboots into the app.
// #define DFU_SYSMEM_ADDR 0x1FFF0000

// --- pin map ----------------------------------------------------------------
// Only the pins the enabled features need. Each module's driver documents
// which macros it expects; a missing one is a compile error in that driver,
// never a silent misconfiguration.
#define LED_PIN         LED_BUILTIN
#define LED_ACTIVE_LOW  0        // 1 when driving the pin LOW lights the LED

// #define BUTTON_PIN   USER_BTN   // required when FEATURE_BUTTON is 1

// Required when FEATURE_ST7789_240X240 is 1. One module per panel type, named
// after the controller and resolution -- a different screen gets its own module
// and its own FEATURE_ flag, so two can never be enabled at once by accident.
// The pin macros stay generic (DISPLAY_*) because only one is ever built in.
// SCK/MOSI come from the SPI peripheral itself, so only the control pins are named. Optional, with defaults in
// st7789_240x240_driver.cpp: DISPLAY_CS (GFX_NOT_DEFINED -- most GMT130 panels bring
// no CS out), DISPLAY_ROTATION (0), DISPLAY_W/H (240), DISPLAY_SPI_HZ (8MHz),
// DISPLAY_INIT_BUDGET_MS (400, past which the startup log line warns).
//
// Note: nothing detects whether the panel is plugged in. It cannot be done
// without a MISO line to read the controller's ID back over, and the driver
// is write-only, so an absent panel costs exactly what a present one does --
// it just shows nothing. See _notes/spec-display.md.
// #define DISPLAY_DC   PB1
// #define DISPLAY_RST  PB0

// Required when FEATURE_SERVO is 1: a hobby servo on one timer channel. The
// timer instance is named here rather than derived from the pin, so which
// timer the module claims is explicit and greppable -- worth the extra macro
// on a board that will grow more timer-driven modules. The channel IS derived
// from the pin, so the two must agree; nothing checks that at compile time.
// SERVO_FRAME_US is optional (20000, i.e. 50Hz, defaulted in the driver);
// raise it only for a digital servo that documents a faster frame.
//
// Power the servo from 5V, never 3V3, with a bulk cap at the connector -- see
// the note in blackpill_f411ce.h.
// #define SERVO_TIMER  TIM4
// #define SERVO_PIN    PB6      // TIM4_CH1

// Required when FEATURE_ESC0 is 1: a brushless ESC on its own timer channel,
// separate from FEATURE_SERVO's timer -- see the note in blackpill_f411ce.h
// for why sharing one is unsafe. The channel IS derived from the pin, so the
// two must agree; nothing checks that at compile time.
// ESC0_FRAME_US (optional, 20000/50Hz), ESC0_ARM_HOLD_MS (optional, 2000),
// ESC0_INPUT_STALE_MS (optional, 500) and ESC0_ARM_LOW_MARGIN_US (optional,
// 50) are all defaulted in esc0_driver.cpp.
//
// A second ESC (FEATURE_ESC1 with ESC1_PIN/ESC1_TIMER, same shape) needs a
// DIFFERENT PHYSICAL TIMER PERIPHERAL from the first, not just a different
// channel of the same one -- two independently-constructed HardwareTimer
// objects sharing one peripheral fight over its shared overflow/period
// register.
//
// Power the motor/ESC from its own supply, never this board's 5V/VBUS pin.
// #define ESC0_TIMER  TIM3
// #define ESC0_PIN    PA6      // TIM3_CH1

// Required when FEATURE_RX is 1: an RC receiver on its own hardware serial
// port, decoded by the protocol-agnostic rx module.
// #define FEATURE_RX      1
// #define RX_RX_PIN       PA10       // receiver's serial TX -> this pin
// #define RX_TX_PIN       PA9        // reserved for a telemetry uplink; unused
// #define RX_BAUD         420000     // CRSF family: Crossfire and ExpressLRS both
// Requires -D SERIAL_RX_BUFFER_SIZE=256 in this env's build_flags -- the
// driver #errors without it. The Arduino default of 64 bytes holds ~2 frames
// and a receiver would tear on nearly every one.
//
// Which protocol is parsed is a RUNTIME setting (rx.protocol), not a build
// flag: one receiver is wired at a time, and swapping an ELRS receiver for a
// Crossfire one should not need a reflash.
