#pragma once
// WeAct Black Pill, STM32F401CE.
//
// Same WeActStudio.MiniSTM32F4x1 board footprint as blackpill_f411ce.h, just a
// different MCU on it: 96KB RAM instead of 128KB, 84MHz max instead of 100MHz.
// Confirmed here because one unit from a batch of spare boards turned out to
// be betacrawlered/sold as an F411CE but actually populated with a genuine
// STM32F401CEU6 -- the F411 env's linker script assumes 128KB RAM and puts
// the initial stack pointer past the real 96KB this chip has, so it
// HardFaults on every boot before USB even comes up. This env exists so that
// board can run firmware built against its actual memory map.
//
// Selected at compile time by platformio.ini:
//   -D BOARD_HEADER='"boards/blackpill_f401ce.h"'
//
// NOTE: the native test environment builds against the F411 header, not this
// one -- see platformio.ini's [env:native]. Only one header can back
// test/golden/schema.json.
//
// This board ships what blackpill_f411ce.h ships, and test_board_headers holds
// the two files to it: same FEATURE_ flags with the same values, no define
// present in one and absent from the other, and shared names agreeing on their
// value. A deliberate difference goes in that suite's exception list, with the
// reason, rather than being left to be noticed.

#define BOARD_ID "blackpill_f401ce"

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
// The F401 has the same USB DFU bootloader in ROM as the F411 -- same system
// memory base, same AN2606 entry. See blackpill_f411ce.h for the rationale.
#define FEATURE_DFU     1

// --- DFU --------------------------------------------------------------------
// Same system-memory base as the F411; the F401/F411/F405/F407/F410/F412/F413
// family shares this address (check the "system memory" row of the device's
// reference manual before reusing this for anything outside that family).
#define DFU_SYSMEM_ADDR 0x1FFF0000

// --- pin map ----------------------------------------------------------------
// WeAct kept the pinout identical across board revisions, so every pin below
// is unchanged from blackpill_f411ce.h -- confirmed against the Arduino core's
// own variant header for this board (LED_BUILTIN and USER_BTN resolve to the
// same PC13 / PA0 as the F411 variant does).
//
// PC13 *sinks* the on-board LED: driving it LOW turns the LED ON.
#define LED_PIN         LED_BUILTIN
#define LED_ACTIVE_LOW  1

// KEY button, pulled up; the driver samples its idle level at boot rather
// than assuming a polarity.
#define BUTTON_PIN      USER_BTN

// Hobby servo on TIM4_CH1 -- but TIM4 is now claimed by esc1 (below), which
// also drives PB6/TIM4_CH1. Same latent conflict as blackpill_f411ce.h: this
// board does not ship FEATURE_SERVO on, so the #error guard just past esc1's
// block below only fires if someone flips FEATURE_SERVO on here without also
// reconsidering esc1.
//
// Power the servo from the 5V pin (USB VBUS), never 3V3, with a 470-1000uF
// bulk cap at the connector -- see the note in blackpill_f411ce.h.
#define SERVO_TIMER     TIM4
#define SERVO_PIN       PB6

// Brushless ESCs on TIM3_CH1 and TIM4_CH1, same pins and reasoning as
// blackpill_f411ce.h's esc0/esc1: two separate timer peripherals, not two
// channels of the same one. FEATURE_SERVO, the only other module that claims
// TIM4 (PB6/TIM4_CH1), is off on this board, so esc1 takes it.
#define ESC0_TIMER      TIM3
#define ESC0_PIN        PA6
#define ESC1_TIMER      TIM4
#define ESC1_PIN        PB6

// 200Hz frame on both, same reasoning as blackpill_f411ce.h. Without these the
// module default of 20000us applies, which is 50Hz.
#define ESC0_FRAME_US   5000
#define ESC1_FRAME_US   5000

// Both esc1 and (if ever enabled) servo drive TIM4/PB6 -- see
// blackpill_f411ce.h's identical guard for the full hazard explanation.
#if FEATURE_SERVO && FEATURE_ESC1
#error "servo and esc1 both claim TIM4/PB6 on this board -- move one to another timer/pin before enabling both"
#endif

// Battery voltage sense on ADC1_IN1, same pin, divider and scale as
// blackpill_f411ce.h; see that header for the ratio and what vbat.scale means.
#define VBAT_PIN        PA1
#define VBAT_SCALE_DEFAULT 11000

// CRSF receiver on USART1. Same pins as blackpill_f411ce.h; see that header
// for why receive is on PB7 rather than USART1's usual PA10, and why transmit
// stays on PA9.
#define RX_RX_PIN       PB7
#define RX_TX_PIN       PA9
#define RX_BAUD         420000
