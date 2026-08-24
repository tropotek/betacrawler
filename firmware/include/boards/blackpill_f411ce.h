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
#define FEATURE_MOTOR0       1
#define FEATURE_MOTOR1       1
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

// Hobby servo on TIM4_CH1 -- but TIM4 is now claimed by motor1 (below), which
// drives PB8/TIM4_CH3, a different channel of the SAME physical peripheral.
// This board does not ship FEATURE_SERVO on, so the conflict is latent, not
// live; the #error guard just past ESC1's block below catches the case where
// someone flips FEATURE_SERVO on here without also reconsidering motor1 -- two
// independently-constructed HardwareTimer objects on TIM4, even on different
// channels, still fight over its shared overflow/period register. A servo
// fork on this board needs a different timer entirely, not any channel of
// TIM4. Note this part is LQFP48, so port C is only PC13/14/15 and PB11 is
// not bonded out -- most of the timer maps a generic F4 pinout table offers
// do not exist here.
//
// SERVO_FRAME_US (20000, i.e. 50Hz) is optional, defaulted in the driver.
//
// Power the servo from the 5V pin (USB VBUS), never 3V3, with a 470-1000uF
// bulk cap at the connector. Current steps from a moving servo can droop VBUS
// far enough to reset the MCU and drop the USB CDC link, which presents as a
// configurator disconnect rather than as anything obviously electrical.
#define SERVO_TIMER     TIM4
#define SERVO_PIN       PB6

// Brushless ESCs on TIM3_CH1 and TIM4_CH3. Each is a separate timer
// peripheral, on purpose -- two independently-constructed HardwareTimer
// objects sharing one physical peripheral would each fight over its shared
// overflow/period register. PA6 and PB8 are confirmed free against this
// part's own PeripheralPins.c
// (framework-arduinoststm32/variants/STM32F4xx/F411C(C-E)(U-Y)/PeripheralPins.c).
//
// MOTOR0_ARM_HOLD_MS/MOTOR1_ARM_HOLD_MS (2000), MOTOR0_INPUT_STALE_MS/
// MOTOR1_INPUT_STALE_MS (500) and MOTOR0_ARM_LOW_MARGIN_US/
// MOTOR1_ARM_LOW_MARGIN_US (50) are all optional per instance, defaulted in
// esc0_driver.cpp/esc1_driver.cpp respectively.
//
// Power the motor/ESC from its own supply, never the board's 5V/VBUS pin --
// an ESC under load draws far more than the servo's own VBUS warning already
// covers.
//
// motor0 on TIM3_CH1 -- the pin already wired and documented on every unit
// shipped so far, unchanged from the single-ESC configuration this board
// used to have.
#define MOTOR0_TIMER      TIM3
#define MOTOR0_PIN        PA6

// motor1 on TIM4_CH3 -- moved off PB6 (2026-08-23) to free that pin for
// WIFI_TX_PIN below; still a DIFFERENT physical timer peripheral from motor0's
// TIM3, not just a different channel of the same one. PB8 is confirmed free.
#define MOTOR1_TIMER      TIM4
#define MOTOR1_PIN        PB8

// motor0/motor1's second PWM pin, used only when that instance's motor<N>.type is
// brushed (a runtime choice -- both macros are always defined whenever
// FEATURE_MOTOR0/FEATURE_MOTOR1 are on, regardless of which type is selected).
// PA7 is TIM3_CH2 -- the same physical timer as motor0's own PA6 (TIM3_CH1),
// a different channel of it, matching how motor1 and the future servo pin
// already share TIM4 across different channels. It must be named as
// PA7_ALT1: PA7's FIRST entry in this part's PinMap_TIM is TIM1_CH1N, and
// the pinmap lookup answers that one for a bare PA7. PB9 is TIM4_CH4, same
// relationship to motor1's PB8 (TIM4_CH3), and its first entry already, so it
// needs no alias. Both bench-validated
// (_notes/docs/research/brushed-tank-variant.md, section 5a). Neither
// carries the ROM-bootloader-race hazard RX_RX_PIN does: motor output is
// always MCU-to-peripheral, never the reverse, so nothing external ever
// transmits into either pin (docs/development/architecture.md, "CRSF pin
// choice and the bootloader race").
#define MOTOR0_PIN_B  PA7_ALT1
#define MOTOR1_PIN_B  PB9

// Which motor each instance starts on -- motor::TYPE_BRUSHLESS (an ESC) or
// motor::TYPE_BRUSHED (an H-bridge). motor<N>.type overrides it at runtime; this
// only decides where an unconfigured board starts, including after a settings
// reset. Both are TYPE_BRUSHLESS because this image serves either wiring: a
// fork committed to brushed motors sets TYPE_BRUSHED here so a reset can never
// leave an H-bridge driven by an RC pulse train (a 1500us pulse in a 5000us
// frame is 30% duty).
#define MOTOR0_TYPE_DEFAULT  motor::TYPE_BRUSHLESS
#define MOTOR1_TYPE_DEFAULT  motor::TYPE_BRUSHLESS

// Battery voltage sense on ADC1_IN1. PA1 is unclaimed on this board: the LED
// is PC13, the button PA0, the ESCs PA6/PB8, CRSF PA2/PA3, USB PA11/PA12 and
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
#define MOTOR0_FRAME_US   5000
#define MOTOR1_FRAME_US   5000

// Both motor1 and (if ever enabled) servo drive TIM4 -- motor1 on CH3 (PB8),
// servo on CH1 (PB6) -- different channels of the SAME physical peripheral,
// which still fight over its shared overflow/period register even though
// they no longer share a pin. FEATURE_SERVO ships 0 on this board today, so
// nothing conflicts yet -- but if someone flips it on here without also
// reconsidering motor1, both servo::ServoDriver and motor1::MotorDriver would
// construct their own HardwareTimer(TIM4) and fight over its shared period
// register. Catch that at compile time instead.
#if FEATURE_SERVO && FEATURE_MOTOR1
#error "servo and motor1 both claim TIM4 on this board -- move one to another timer/pin before enabling both"
#endif

// CRSF receiver on USART2's native pins. Both DFU entry paths reboot into
// the STM32 ROM bootloader, which arms multiple peripherals at once, each
// watched for a specific autobaud sync byte (0x7F, even parity -- AN3155),
// not "any traffic". Bench-testing (2026-08-23,
// _notes/docs/research/rx-uart-bootloader-race.md) found real, linked ELRS
// traffic never triggers a hijack on PA3, matching PA10 and PB7's own
// results for the same receiver -- but a deliberate flood of the trigger
// byte blocks DFU on every UART pin on this package tested so far, so
// nothing here is safe by virtue of its pin alone. Full reasoning:
// docs/development/architecture.md, "CRSF pin choice and the bootloader
// race".
//
// PA2/PA3 chosen over USART1's own pins (default PA9/PA10, or alternate
// PB6/PB7) specifically to leave PA9/PA10 completely standard and unclaimed
// -- a fork's own project gets a real, unremapped spare UART. USART1's
// alternate mapping went to WIFI_RX_PIN/WIFI_TX_PIN below instead.
//
// The driver constructs its own HardwareSerial from these pins rather than
// using a global Serial2, which the STM32 core only defines when the variant
// declares PIN_SERIAL2_RX/TX.
#define RX_RX_PIN       PA3
// Telemetry back to the handset: the rx module transmits CRSF battery frames
// here from the core::Battery bus.
#define RX_TX_PIN       PA2
// The TBS specification gives 416666 for the dual-wire vehicle-side link;
// Betaflight and everyone else use 420000. They are 0.8% apart, well inside
// UART tolerance, and either talks to either. A board pairing with a 400k
// half-duplex link changes this number here rather than in any source file.
#define RX_BAUD         420000
//
// Wiring: receiver 5V and GND from the board's 5V pin, receiver CRSF TX ->
// PA3. The Nano RX's pads default to PWM output -- one must be reassigned to
// CRSF in the TBS menu before anything appears on the wire at all.

// ESP-01 (ESP8266) WiFi module, stock AT firmware, on USART1's alternate
// mapping. Moved here 2026-08-23 when CRSF moved to PA2/PA3 (USART2),
// freeing PB6/PB7 -- motor1 no longer claims PB6 (see its own comment above).
// Bench-testing (_notes/docs/research/rx-uart-bootloader-race.md)
// characterized CRSF's own receiver traffic against this pin pair's
// bootloader-hijack risk, not the ESP8266's AT-firmware chatter.
// FEATURE_WIFI ships 0 by default -- this pair should get the same
// real-hardware flood test CRSF's pins got before anyone relies on it with
// WiFi turned on in a shipped build.
// CH_PD, GPIO0, GPIO2 and RST are pulled high locally on the module side
// (10k to 3V3) and do not connect to any STM32 pin -- see the wiring
// diagram referenced from _notes/spec-wifi.md.
#define WIFI_RX_PIN  PB7
#define WIFI_TX_PIN  PB6
#define WIFI_BAUD    115200
