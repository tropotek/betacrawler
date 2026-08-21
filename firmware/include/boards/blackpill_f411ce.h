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
#define FEATURE_STATUS_LED  1
#define FEATURE_BUTTON  0
#define FEATURE_SERVO   0
#define FEATURE_RX         1
#define FEATURE_TANK_DRIVE 1
#define FEATURE_ESC0       1
#define FEATURE_ESC1       1
#define FEATURE_VBAT       1
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

// Hobby servo on TIM4_CH1 -- but TIM4 is now claimed by esc1 (below), which
// also drives PB6/TIM4_CH1. This board does not ship FEATURE_SERVO on, so the
// conflict is latent, not live; the #error guard just past ESC1's block below
// catches the case where someone flips FEATURE_SERVO on here without also
// reconsidering esc1. A servo fork on this board needs a different timer,
// not TIM4 -- CH2-4 (PB7/PB8/PB9) of the SAME peripheral don't help, since
// esc1 already owns the peripheral's shared overflow/period register.
// Nothing else here touches PB6/7/8/9: the LED is PC13, the button PA0,
// USB PA11/PA12 and SWD PA13/PA14. Note this part is LQFP48, so port C is
// only PC13/14/15 and PB11 is not bonded out -- most of the timer maps a
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
// ESC0_ARM_HOLD_MS/ESC1_ARM_HOLD_MS (2000), ESC0_INPUT_STALE_MS/
// ESC1_INPUT_STALE_MS (500) and ESC0_ARM_LOW_MARGIN_US/
// ESC1_ARM_LOW_MARGIN_US (50) are all optional per instance, defaulted in
// esc0_driver.cpp/esc1_driver.cpp respectively.
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

// Battery voltage sense on ADC1_IN1. PA1 is unclaimed on this board: the LED
// is PC13, the button PA0, the ESCs PA6/PB6, CRSF PA9/PB7, USB PA11/PA12 and
// SWD PA13/PA14. Expects a 47k/4k7 divider from the PDB's VCC pad; vbat.scale
// is the runtime calibration.
#define VBAT_PIN        PA1

// x1000 multiplier from the sense pin to pack millivolts, and the default for
// vbat.scale. 11000 is this build's own 47k/4k7 divider: 4.7/51.7 is exactly
// 1:11, inverted. A board reading a PDB's built-in sense output instead states
// that PDB's ratio here -- 10000 for a 1:10 output -- since the divider is a
// property of the hardware, not of the firmware. Calibrating corrects it
// either way; this only decides where an uncalibrated board starts.
#define VBAT_SCALE_DEFAULT 11000

// 200Hz frame on both, which every analogue-PWM ESC auto-detects. A 5ms
// period bounds output latency at a quarter of a 50Hz frame's, and
// effectiveMaxUs()'s reserved low time leaves ample headroom over max_us.
#define ESC0_FRAME_US   5000
#define ESC1_FRAME_US   5000

// Both esc1 and (if ever enabled) servo drive TIM4/PB6. FEATURE_SERVO ships
// 0 on this board today, so nothing conflicts yet -- but if someone flips it
// on here without also reconsidering esc1, both servo::ServoDriver and
// esc1::EscDriver would construct their own HardwareTimer(TIM4) and fight
// over PB6, with the servo's writes landing on the ESC output behind esc1's
// arm-hold safety gate, silently. Catch that at compile time instead.
#if FEATURE_SERVO && FEATURE_ESC1
#error "servo and esc1 both claim TIM4/PB6 on this board -- move one to another timer/pin before enabling both"
#endif

// CRSF receiver on USART1, receiving on PB7 rather than USART1's usual PA10.
// The ROM bootloader auto-selects its host interface and commits to whichever
// shows traffic first; a powered receiver on PA10 wins that race every time and
// USB DFU then never enumerates. PB7 is USART1_RX on its alternate mapping and
// is only an I2C1 candidate to the bootloader, which cannot commit without a
// master clocking SCL. Transmit stays on PA9: it is outbound only, so it never
// triggers detection. esc1 keeps PB6.
//
// The driver constructs its own HardwareSerial from these pins rather than
// using a global Serial1, which the STM32 core only defines when the variant
// declares PIN_SERIAL1_RX/TX.
#define RX_RX_PIN       PB7
// Telemetry back to the handset: the rx module transmits CRSF battery frames
// here from the core::Battery bus.
#define RX_TX_PIN       PA9
// The TBS specification gives 416666 for the dual-wire vehicle-side link;
// Betaflight and everyone else use 420000. They are 0.8% apart, well inside
// UART tolerance, and either talks to either. A board pairing with a 400k
// half-duplex link changes this number here rather than in any source file.
#define RX_BAUD         420000
//
// Wiring: receiver 5V and GND from the board's 5V pin, receiver CRSF TX ->
// PB7. The Nano RX's pads default to PWM output -- one must be reassigned to
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
