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
//        lib_deps     = bblanchon/ArduinoJson@^7.0.4
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
#define FEATURE_LED    1
#define FEATURE_BUTTON 0

// --- pin map ----------------------------------------------------------------
// Only the pins the enabled features need. Each module's driver documents
// which macros it expects; a missing one is a compile error in that driver,
// never a silent misconfiguration.
#define LED_PIN         LED_BUILTIN
#define LED_ACTIVE_LOW  0        // 1 when driving the pin LOW lights the LED

// #define BUTTON_PIN   USER_BTN   // required when FEATURE_BUTTON is 1
