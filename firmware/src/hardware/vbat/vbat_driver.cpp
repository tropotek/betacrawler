#include "hardware/vbat/vbat_driver.h"
#include "hardware/vbat/vbat_math.h"
#include "core/boot_log.h"
#include "core/triangle.h"
#include "config.h"

// Guards the body, not just the class: PlatformIO compiles every .cpp under
// src/ regardless of what includes it.
#if FEATURE_VBAT

#include <Arduino.h>
#include "hardware/adc/adc_vref.h"

#ifndef VBAT_PIN
#error "FEATURE_VBAT is on but the board header defines no VBAT_PIN"
#endif

namespace vbat {

// A full sweep of the simulated pack. Slow enough to watch, fast enough to
// exercise both directions inside a bench session.
constexpr uint32_t kSimPeriodMs = 60000;

// Long enough for the sense node to follow an internal pull. A fitted divider
// carries a 100nF filter cap, which the ~40k internal pull moves slowly.
constexpr uint32_t kProbeSettleMs = 5;
constexpr uint16_t kSimLowMv    = 13200;
constexpr uint16_t kSimHighMv   = 16800;

void VbatDriver::attach(const core::Registry& reg, const core::Params& p) {
  (void)reg;
  // Params already holds whatever main.cpp's store.load() restored, so this is
  // real persisted state. onParamChanged() only fires on a LATER change.
  source_   = p.num(globalParam(P_SOURCE));
  scale_    = p.num(globalParam(P_SCALE));
  cellsSel_ = p.num(globalParam(P_CELLS));
  if (cellsSel_ != CELLS_AUTO) cells_ = (uint8_t)(cellsSel_ + 1);
}

// Is anything actually on the sense pin? A pin with no divider fitted floats,
// and a floating ADC input does not read zero -- it drifts to roughly a third
// of the supply, which through the divider's multiplier is a plausible pack
// voltage that nothing downstream can tell from a real one.
//
// Probed digitally rather than by reading the ADC: on this part a pin in analog
// mode has its pull resistors disabled in hardware, so the two cannot be
// combined. A floating pin FOLLOWS whichever pull is applied. The divider's
// low-side resistor is an order of magnitude stronger than the internal pull,
// so a fitted one holds the pin against both. Comparing the two reads sidesteps
// the indeterminate logic band entirely -- what matters is whether the pin
// moved, not what level either read reported.
bool VbatDriver::senseWired() {
  pinMode(VBAT_PIN, INPUT_PULLDOWN);
  delay(kProbeSettleMs);
  const bool followedDown = (digitalRead(VBAT_PIN) == LOW);
  pinMode(VBAT_PIN, INPUT_PULLUP);
  delay(kProbeSettleMs);
  const bool followedUp = (digitalRead(VBAT_PIN) == HIGH);
  // analogRead() reconfigures the pin as analog on every call, so there is
  // nothing to restore here.
  return !(followedDown && followedUp);
}

void VbatDriver::begin() {
  if (source_ == SRC_SIM) {
    core::bootLog().add("vbat source=sim (synthetic, no divider)");
    return;
  }
  if (source_ != SRC_ADC) return;
  wired_ = senseWired();
  if (!wired_)
    core::bootLog().add("vbat: sense pin floating, no divider fitted -- not reading");
}

void VbatDriver::onParamChanged(uint8_t local, const core::Params& p) {
  switch (local) {
    case P_SOURCE: {
      const int32_t v = p.num(globalParam(P_SOURCE));
      if (v != source_) {
        // Everything measured under the previous source is meaningless now.
        mv_ = 0; pct_ = 0; simT0_ = 0;
        latch_.reset();
        if (cellsSel_ == CELLS_AUTO) cells_ = 0;
        out_.set(0, cells_, 0);
        // Re-probe on the way in: the divider may have been fitted since boot.
        if (v == SRC_ADC) wired_ = senseWired();
      }
      source_ = v;
      break;
    }
    case P_SCALE: {
      const int32_t v = p.num(globalParam(P_SCALE));
      // Every reading so far was scaled by the old multiplier, so a cell count
      // derived from them no longer follows -- calibrating must re-detect.
      if (v != scale_) {
        latch_.reset();
        if (cellsSel_ == CELLS_AUTO) cells_ = 0;
      }
      scale_ = v;
      break;
    }
    case P_CELLS:
      // An explicit selection replaces the latch at once; returning to auto
      // re-arms detection on the next valid reading.
      cellsSel_ = p.num(globalParam(P_CELLS));
      latch_.reset();
      cells_ = (cellsSel_ == CELLS_AUTO) ? 0 : (uint8_t)(cellsSel_ + 1);
      break;
    default:
      break;
  }
}

uint16_t VbatDriver::simMv(uint32_t nowMs) {
  if (simT0_ == 0) simT0_ = nowMs;
  const uint32_t t = nowMs - simT0_;
  const uint16_t span = kSimHighMv - kSimLowMv;
  return (uint16_t)(kSimLowMv +
      (uint32_t)core::trianglePercent(t % kSimPeriodMs, kSimPeriodMs) * span / 100);
}

// Tap millivolts from the pin, then the calibrated multiplier. vddaMv() comes
// from VREFINT, so the reading self-corrects for a supply that is not exactly
// 3.3V rather than assuming a nominal rail.
uint16_t VbatDriver::adcMv() {
  analogReadResolution(12);
  const int32_t raw = analogRead(VBAT_PIN);
  if (raw <= 0) return 0;
  const uint16_t tapMv = (uint16_t)((raw * adcref::vddaMv()) / 4095);
  return packMvFromTap(tapMv, scale_);
}

void VbatDriver::publish(uint16_t packMv, uint32_t nowMs) {
  mv_ = packMv;
  // Latch once and never re-evaluate: a pack sagging under load would
  // otherwise flip the cell count mid-drive and make percent jump. A settled 0
  // is different -- the pack is gone, so the count goes with it rather than
  // being inherited by whatever is connected next.
  const uint8_t settled = latch_.update(packMv, nowMs);
  if (settled == 0) cells_ = 0;
  else if (settled != CellLatch::kUnsettled && cells_ == 0) cells_ = settled;
  pct_ = remainingPct(mv_, cells_);
  out_.set(mv_, cells_, pct_);
  out_.markFresh(nowMs);
}

void VbatDriver::tick(uint32_t nowMs) {
  switch (source_) {
    case SRC_SIM:
      publish(simMv(nowMs), nowMs);
      break;
    case SRC_ADC:
      // Nothing wired: publish nothing at all, exactly as SRC_OFF does, rather
      // than a floating pin's reading dressed up as a pack.
      if (wired_) publish(adcMv(), nowMs);
      break;
    default:
      break;   // SRC_OFF publishes nothing at all, so rx sends no frame
  }
}

void VbatDriver::readTelemetry(core::TlmValue* out) {
  out[T_MV].u    = mv_;
  out[T_CELLS].u = cells_;
  out[T_PCT].u   = pct_;
}

}  // namespace vbat

#endif  // FEATURE_VBAT
