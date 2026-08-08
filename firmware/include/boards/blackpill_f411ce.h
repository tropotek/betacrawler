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
#define FEATURE_ST7789_240X240 0
#define FEATURE_SERVO   0
#define FEATURE_RX      1
#define FEATURE_ESC0    1
#define FEATURE_ESC1    1
#define FEATURE_WIFI    0
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

// Hobby servo on TIM4_CH1. TIM4 is chosen because all four of its channels
// (PB6/PB7/PB8/PB9, AF2) are free on this board and contiguous on the header,
// so a multi-channel fork is additive -- CH2-4 stay unclaimed. Nothing else
// here touches them: the LED is PC13, the button PA0, the panel PA5/PA7 +
// PB0/PB1, USB PA11/PA12 and SWD PA13/PA14. Note this part is LQFP48, so port
// C is only PC13/14/15 and PB11 is not bonded out -- most of the timer maps a
// generic F4 pinout table offers do not exist here.
//
// SERVO_FRAME_US (20000, i.e. 50Hz) is optional, defaulted in the driver.
//
// Power the servo from the 5V pin (USB VBUS), never 3V3, with a 470-1000uF
// bulk cap at the connector. Current steps from a moving servo can droop VBUS
// far enough to reset the MCU and drop the USB CDC link, which presents as a
// configurator disconnect rather than as anything obviously electrical.
#define SERVO_TIMER     TIM4
#define SERVO_PIN       PB6

// Brushless ESCs on TIM3_CH1 and TIM4_CH1. Each is a separate timer
// peripheral, on purpose -- two independently-constructed HardwareTimer
// objects sharing one physical peripheral would each fight over its shared
// overflow/period register. PA6 is confirmed free against this part's own
// PeripheralPins.c
// (framework-arduinoststm32/variants/STM32F4xx/F411C(C-E)(U-Y)/PeripheralPins.c):
// nothing else here claims it. FEATURE_SERVO, the only other module that
// claims TIM4 (PB6/TIM4_CH1), is off on this board, so esc1 takes it.
//
// ESC_FRAME_US (20000, i.e. 50Hz), ESC_ARM_HOLD_MS (2000), ESC_INPUT_STALE_MS
// (500) and ESC_ARM_LOW_MARGIN_US (50) are all optional, defaulted in
// esc0_driver.cpp/esc1_driver.cpp.
//
// Power the motor/ESC from its own supply, never the board's 5V/VBUS pin --
// an ESC under load draws far more than the servo's own VBUS warning already
// covers.
//
// esc0 on TIM3_CH1 -- the pin already wired and documented on every unit
// shipped so far, unchanged from the single-ESC configuration this board
// used to have.
#define ESC0_TIMER      TIM3
#define ESC0_PIN        PA6

// esc1 on TIM4_CH1 -- a DIFFERENT physical timer peripheral from esc0's
// TIM3, not a second channel of the same one (two independently-constructed
// HardwareTimer objects sharing one peripheral fight over its shared
// overflow/period register). TIM4 is free on this board: FEATURE_SERVO,
// the only other module that claims it (PB6/TIM4_CH1), is off here.
#define ESC1_TIMER      TIM4
#define ESC1_PIN        PB6

// CRSF receiver on USART1. PA9/PA10 are the only unclaimed peripheral pins on
// this board and nothing else here references them: the LED is PC13, the
// button PA0, the panel PA5/PA7 + PB0/PB1, the servo PB6, USB PA11/PA12 and
// SWD PA13/PA14. USART1's ALTERNATE mapping is PB6/PB7, which would collide
// with the servo output -- so this mapping, not that one.
//
// The driver constructs its own HardwareSerial from these pins rather than
// using a global Serial1, which the STM32 core only defines when the variant
// declares PIN_SERIAL1_RX/TX.
#define RX_RX_PIN       PA10
// Reserved, unused in phase 1. Sending telemetry back to the handset (battery,
// GPS, flight mode) is the natural next use of this peripheral, and claiming
// the pin now is cheaper than discovering it taken later.
#define RX_TX_PIN       PA9
// The TBS specification gives 416666 for the dual-wire vehicle-side link;
// Betaflight and everyone else use 420000. They are 0.8% apart, well inside
// UART tolerance, and either talks to either. A board pairing with a 400k
// half-duplex link changes this number here rather than in any source file.
#define RX_BAUD         420000
//
// Wiring: receiver 5V and GND from the board's 5V pin, receiver CRSF TX ->
// PA10. The Nano RX's pads default to PWM output -- one must be reassigned to
// CRSF in the TBS menu before anything appears on the wire at all.

// ESP-01 (ESP8266) WiFi module, stock AT firmware, on USART2. PA2/PA3 are
// free on this board regardless of FEATURE_RX/FEATURE_ESC0/FEATURE_ESC1 --
// deliberately not reusing RX's USART1 pins, so a future board can enable
// both at once.
// CH_PD, GPIO0, GPIO2 and RST are pulled high locally on the module side
// (10k to 3V3) and do not connect to any STM32 pin -- see the wiring
// diagram referenced from _notes/spec-wifi.md.
#define WIFI_RX_PIN  PA3
#define WIFI_TX_PIN  PA2
#define WIFI_BAUD    115200
