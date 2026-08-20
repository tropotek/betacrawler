#pragma once
#include <stdint.h>

// Pure battery arithmetic. Zero Arduino includes -- the native build compiles
// this, and every rule below is asserted in test_vbat.
namespace vbat {

// Below this the board is on USB with no pack and a floating pin. Nothing is
// detected and nothing is claimed.
constexpr uint16_t kMinValidMv = 5000;

// Cell detection divides by just above a full cell, so every CHARGED pack
// resolves correctly. A part-drained pack reads low -- a 6S at or below
// 21500mV misreads as 5S, which is why vbat.cells offers an explicit setting.
constexpr uint16_t kCellDetectMv = 4300;

constexpr uint16_t kCellEmptyMv = 3300;
constexpr uint16_t kCellFullMv  = 4200;

// Tap millivolts -> pack millivolts. scale is a x1000 multiplier, so the
// 47k/5k6 divider's 9.3929 is stored as 9393.
uint16_t packMvFromTap(uint16_t tapMv, int32_t scale);

// ceil(packMv / kCellDetectMv), or 0 below kMinValidMv.
uint8_t detectCells(uint16_t packMv);

// Linear across kCellEmptyMv..kCellFullMv per cell, clamped. 0 when cells is
// 0. Linear over a non-linear discharge curve, so it overstates mid-pack --
// adequate for a handset indicator, not a fuel gauge.
uint8_t remainingPct(uint16_t packMv, uint8_t cells);

}  // namespace vbat
