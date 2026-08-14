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
// test/golden/schema.json. This header's feature block is kept in step with
// blackpill_f411ce.h's by hand (button/led/rx/esc0/esc1 all on here too) --
// check both headers' feature blocks before assuming parity still holds, since
// nothing enforces it automatically.

#define BOARD_ID "blackpill_f401ce"

// --- features ---------------------------------------------------------------
#define FEATURE_STATUS_LED  1
#define FEATURE_BUTTON  0
#define FEATURE_ST7789_240X240 0
#define FEATURE_SERVO   0
#define FEATURE_RX         1
#define FEATURE_TANK_DRIVE 0
#define FEATURE_ESC0       1
#define FEATURE_ESC1       1
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

// ST7789 240x240 IPS (GMT130) on hardware SPI1: SCK=PA5, MOSI=PA7 come from
// the SPI peripheral itself, so only the two control pins are named here.
// The module brings out no CS (DISPLAY_CS defaults to GFX_NOT_DEFINED) and
// BLK is tied to 3V3, so there is no backlight control either.
#define DISPLAY_DC      PB1
#define DISPLAY_RST     PB0
#define DISPLAY_ROTATION 0
// 24MHz target, same as the F411 env. The F401 tops out at 84MHz rather than
// 100MHz, so SPI1's prescaler quantises this to a different actual value than
// the F411's 96/4 -- still well under the ST7789's tolerance either way. Lower
// it here if a long or noisy ribbon shows artifacts.
#define DISPLAY_SPI_HZ  24000000

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

// Both esc1 and (if ever enabled) servo drive TIM4/PB6 -- see
// blackpill_f411ce.h's identical guard for the full hazard explanation.
#if FEATURE_SERVO && FEATURE_ESC1
#error "servo and esc1 both claim TIM4/PB6 on this board -- move one to another timer/pin before enabling both"
#endif

// CRSF receiver on USART1. Same pins as blackpill_f411ce.h; see that header
// for why PA9/PA10 and not the ALTERNATE PB6/PB7 mapping.
#define RX_RX_PIN       PA10
#define RX_TX_PIN       PA9
#define RX_BAUD         420000
