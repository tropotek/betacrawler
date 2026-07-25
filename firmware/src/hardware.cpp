#include <Arduino.h>
#include "hardware.h"
#include "core/led_curve.h"

namespace hw {

// STM32F411 factory calibration (reference manual)
#define VREFINT_CAL  (*((uint16_t *)0x1FFF7A2AU))
#define TS_CAL1      (*((uint16_t *)0x1FFF7A2CU))
#define TS_CAL2      (*((uint16_t *)0x1FFF7A2EU))
static const int32_t CAL_VDDA_MV = 3300;
static const int32_t TS_CAL1_TEMP = 30;
static const int32_t TS_CAL2_TEMP = 110;

extern "C" char *sbrk(int incr);
static int freeRamBytes() {
  char top;
  return (int)(&top - (char *)sbrk(0));
}

static int32_t readVddaMv() {
  analogReadResolution(12);
  int32_t raw = analogRead(AVREF);
  if (raw <= 0) return CAL_VDDA_MV;
  return (CAL_VDDA_MV * (int32_t)VREFINT_CAL) / raw;
}

static float readTempC(int32_t vddaMv) {
  analogReadResolution(12);
  int32_t raw = analogRead(ATEMP);
  int32_t adj = (raw * vddaMv) / CAL_VDDA_MV;
  int32_t span = (int32_t)TS_CAL2 - (int32_t)TS_CAL1;
  if (span == 0) return 0.0f;
  return (float)(adj - (int32_t)TS_CAL1) * (TS_CAL2_TEMP - TS_CAL1_TEMP) / span
         + TS_CAL1_TEMP;
}

// --- LED: PC13 is ACTIVE-LOW (LOW = on) and has no timer channel -----------

// Software PWM carrier for fade mode: 2000us (500Hz) -- well above the
// flicker-fusion threshold and far faster than the slowest breathing
// cycle (1000/hz ms, minimum 50ms at hz=20).
static const uint32_t kFadeCarrierUs = 2000;

void LedDriver::write(bool on) { digitalWrite(LED_BUILTIN, on ? LOW : HIGH); }

void LedDriver::begin() {
  pinMode(LED_BUILTIN, OUTPUT);
  write(false);
}

void LedDriver::apply(int32_t modeIdx, int32_t blinkHz) {
  mode_ = modeIdx;
  hz_ = blinkHz < 1 ? 1 : blinkHz;
  if (mode_ == 0) { on_ = false; write(false); }
  else if (mode_ == 1) { on_ = true; write(true); }
}

void LedDriver::tick(uint32_t nowMs) {
  if (mode_ == 2) {
    uint32_t halfPeriod = 500u / (uint32_t)hz_;   // hz_ full cycles per second
    if (halfPeriod == 0) halfPeriod = 1;
    if (nowMs - lastToggle_ >= halfPeriod) {
      lastToggle_ = nowMs;
      on_ = !on_;
      write(on_);
    }
  } else if (mode_ == 3) {
    uint32_t periodMs = 1000u / (uint32_t)hz_;    // one full breath per hz_ seconds
    if (periodMs == 0) periodMs = 1;
    uint32_t phaseMs = nowMs % periodMs;
    uint8_t duty = core::breathingDuty(phaseMs, periodMs);
    uint32_t carrierUs = micros() % kFadeCarrierUs;
    bool shouldBeOn = carrierUs < (kFadeCarrierUs * (uint32_t)duty) / 100u;
    if (shouldBeOn != on_) {
      on_ = shouldBeOn;
      write(on_);
    }
  }
}

static bool btnIdle = true;

void begin() {
  pinMode(USER_BTN, INPUT_PULLUP);
  btnIdle = digitalRead(USER_BTN);   // assume not pressed at boot
}

uint8_t buttonPressed() {
  return (digitalRead(USER_BTN) != btnIdle) ? 1 : 0;
}

void readTelemetry(core::Telemetry* t) {
  int32_t vdd = readVddaMv();
  t->up   = millis();
  t->clk  = SystemCoreClock / 1000000UL;
  t->temp = readTempC(vdd);
  t->vdd  = vdd;
  t->ram  = freeRamBytes();
  t->btn  = buttonPressed();
}

}  // namespace hw
