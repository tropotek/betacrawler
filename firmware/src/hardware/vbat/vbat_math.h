#pragma once
#include <stdint.h>

// Pure battery arithmetic. Zero Arduino includes -- the native build compiles
// this, and every rule below is asserted in test_vbat.
namespace vbat {

// The lowest reading that can be a real pack: 2S at its 3.0V/cell floor. A
// USB-powered board wired to a PDB reads around 4600mV with no pack connected,
// from the BEC back-feeding its own input, which must never count as a battery.
constexpr uint16_t kMinValidMv = 6000;

// Cell detection divides by just above a full cell, so every CHARGED pack
// resolves correctly. A part-drained pack reads low -- a 6S at or below
// 21500mV misreads as 5S, which is why vbat.cells offers an explicit setting.
constexpr uint16_t kCellDetectMv = 4300;

constexpr uint16_t kCellEmptyMv = 3300;
constexpr uint16_t kCellFullMv  = 4200;

// Tap millivolts -> pack millivolts. scale is a x1000 multiplier, so the
// 47k/4k7 divider's 11.0 is stored as 11000.
uint16_t packMvFromTap(uint16_t tapMv, int32_t scale);

// ceil(packMv / kCellDetectMv), or 0 below kMinValidMv.
uint8_t detectCells(uint16_t packMv);

// Linear across kCellEmptyMv..kCellFullMv per cell, clamped. 0 when cells is
// 0. Linear over a non-linear discharge curve, so it overstates mid-pack --
// adequate for a handset indicator, not a fuel gauge.
uint8_t remainingPct(uint16_t packMv, uint8_t cells);

// How long a cell count must hold before it is trusted. A duration, not a
// sample count: the driver ticks at loop rate, so any plausible sample count is
// over in under a millisecond, while plugging a pack in ramps the reading up
// through the lower cell bands and bounces the connector for far longer.
constexpr uint32_t kCellConfirmMs = 500;

// Confirms detectCells() over time. The driver latches a count once and never
// revisits it, so a reading that is merely passing through must not fix it.
// update() returns 0 until the same non-zero count has held for kCellConfirmMs;
// any disagreeing or invalid reading restarts the window.
class CellLatch {
 public:
  uint8_t update(uint16_t packMv, uint32_t nowMs);
  void    reset();

 private:
  uint8_t  candidate_ = 0;
  uint32_t since_     = 0;
};

}  // namespace vbat
