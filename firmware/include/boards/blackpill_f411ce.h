#pragma once
// WeAct Black Pill, STM32F411CE.
//
// Selected at compile time by platformio.ini:
//   -D BOARD_HEADER='"boards/blackpill_f411ce.h"'
//
// NOTE: the native test environment deliberately builds against THIS header
// too, so `pio test -e native` validates the same parameter/telemetry set the
// real board exposes (that is what keeps test/golden/schema.json honest). The
// pin macros below are never expanded natively, because no driver .cpp is
// compiled there -- only the pure *_params.cpp descriptor files are.

#define BOARD_ID "blackpill_f411ce"

// --- features ---------------------------------------------------------------
#define FEATURE_LED     1
#define FEATURE_BUTTON  1
#define FEATURE_ST7789_240X240 1
// Reboot-to-bootloader for in-app firmware updates. The F411 has a USB DFU
// bootloader in ROM, so this costs a magic word and a reset -- no bootloader
// to flash, and nothing to erase it. Turning it off only removes the app's
// one-click path; BOOT0 + NRST still reaches the same ROM code.
#define FEATURE_DFU     1

// --- DFU --------------------------------------------------------------------
// System-memory base, where the STM32 ROM bootloader lives. This is
// family-specific (0x1FFF0000 on the F411; an F103 or an H7 differ), so it
// belongs here rather than in src/dfu.cpp -- porting to another STM32 is then
// a header edit, not a source edit. Check the "system memory" row of the
// device's reference manual before copying this to a new part.
#define DFU_SYSMEM_ADDR 0x1FFF0000

// --- pin map ----------------------------------------------------------------
// Deferred to the Arduino variant's own names (LED_BUILTIN = PC13,
// USER_BTN = PA0) rather than hardcoding pin numbers, so this stays correct
// if the variant is ever revised. A board with no Arduino variant would put
// literal pins here instead -- that indirection is the point of the header.
//
// PC13 *sinks* the on-board LED: driving it LOW turns the LED ON.
#define LED_PIN         LED_BUILTIN
#define LED_ACTIVE_LOW  1

// KEY button, pulled up; the driver samples its idle level at boot rather
// than assuming a polarity.
#define BUTTON_PIN      USER_BTN

// ST7789 240x240 IPS (GMT130) on hardware SPI1: SCK=PA5, MOSI=PA7 come from
// the SPI peripheral itself, so only the two control pins are named here.
// The module brings out no CS (DISPLAY_CS defaults to GFX_NOT_DEFINED) and
// BLK is tied to 3V3, so there is no backlight control either.
//
// Nothing detects whether the panel is actually plugged in -- it cannot be
// done on this wiring, there being no MISO to read an ID back over. The
// driver is write-only and so costs the same either way; see
// _notes/spec-display.md.
#define DISPLAY_DC      PB1
#define DISPLAY_RST     PB0
#define DISPLAY_ROTATION 0
// 24MHz: the STM32F411 SPI1 prescaler quantises this to 96/4. The ST7789
// tolerates far more, and at the old 8MHz (which quantised down to 6MHz) a
// cycle-mode page flip stalled telemetry 353ms -- past the frontend's 300ms
// staleness threshold at tlm.rate 10, i.e. a false "stale" badge every 5s.
// Lower it here if a long or noisy ribbon shows artifacts.
#define DISPLAY_SPI_HZ  24000000
