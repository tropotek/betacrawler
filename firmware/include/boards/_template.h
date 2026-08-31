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
// A non-STM32 MCU family needs more than swapping `platform`, and this template is
// STM32-only. Two things such a port has to solve that it doesn't cover:
//   (a) Any shared file with an MCU-specific body (storage.cpp, system_driver.cpp,
//       ...) needs a twin for the new family, each guarded by its own
//       `FW_MCU_<FAMILY>` macro, if that MCU has peripheral APIs the existing
//       body can't reuse.
//   (b) If the platform's default C++ standard is older than the rest of the tree,
//       add `-std=gnu++17` to build_flags AND the matching `build_unflags` -- the
//       framework appends its own -std after build_flags, so the flag alone is
//       silently overridden without the unflag.
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
#define FEATURE_STATUS_LED  1
// The servo follows the drive mixer's steering slot and has no source of its
// own, so FEATURE_SERVO 1 requires FEATURE_DRIVE 1.
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

// Required when FEATURE_MOTOR0 is 1: a brushless ESC on its own timer channel,
// separate from FEATURE_SERVO's timer -- see the note in blackpill_f411ce.h
// for why sharing one is unsafe. The channel IS derived from the pin, so the
// two must agree; nothing checks that at compile time.
// MOTOR0_FRAME_US (optional, 20000/50Hz), MOTOR0_ARM_HOLD_MS (optional, 2000),
// MOTOR0_INPUT_STALE_MS (optional, 500) and MOTOR0_ARM_LOW_MARGIN_US (optional,
// 50) are all defaulted in motor0_driver.cpp.
//
// A second ESC (FEATURE_MOTOR1 with MOTOR1_PIN/MOTOR1_TIMER, same shape) needs a
// DIFFERENT PHYSICAL TIMER PERIPHERAL from the first, not just a different
// channel of the same one -- two independently-constructed HardwareTimer
// objects sharing one peripheral fight over its shared overflow/period
// register.
//
// Power the motor/ESC from its own supply, never this board's 5V/VBUS pin.
// #define MOTOR0_TIMER  TIM3
// #define MOTOR0_PIN    PA6      // TIM3_CH1

// Required when FEATURE_DRIVE is 1: no pins, and one optional macro -- this
// module touches no hardware, only rx's bus and its own. The one thing that
// DOES matter: in src/modules.cpp's registerModules(), it must register after
// rx and before motor0/motor1, or they will mix stale (one-loop-old)
// throttle/steer data. See drive_driver.cpp's own comment at the registration
// site before reordering anything.
//
// DRIVE_MODE_DEFAULT is optional (drive::MODE_SKID): the mixer this board
// starts on. drive::MODE_CAR suits a board wired as one driven motor plus a
// steering servo. drive.mode overrides it at runtime; this only decides where
// an unconfigured board starts, including after a settings reset.
// #define DRIVE_MODE_DEFAULT  drive::MODE_SKID

// Required when FEATURE_RX is 1: an RC receiver on its own hardware serial
// port, decoded by the protocol-agnostic rx module.
// #define FEATURE_RX      1
// #define RX_RX_PIN       PB7        // receiver's serial TX -> this pin
// Keep receive OFF the pins the ROM bootloader watches for a host (USART1's
// PA10, USART2's PA3): it commits to whichever interface shows traffic first,
// and a powered receiver there wins that race, after which USB never
// enumerates and DFU is unreachable.
// #define RX_TX_PIN       PA9        // reserved for a telemetry uplink; unused
// #define RX_BAUD         420000     // CRSF family: Crossfire and ExpressLRS both
// Requires -D SERIAL_RX_BUFFER_SIZE=256 in this env's build_flags -- the
// driver #errors without it. The Arduino default of 64 bytes holds ~2 frames
// and a receiver would tear on nearly every one.
//
// Which protocol is parsed is a RUNTIME setting (rx.protocol), not a build
// flag: one receiver is wired at a time, and swapping an ELRS receiver for a
// Crossfire one should not need a reflash.
