#include "hardware/vbat/vbat_math.h"

namespace vbat {

uint16_t packMvFromTap(uint16_t tapMv, int32_t scale) {
  if (scale <= 0) return 0;
  const uint32_t v = ((uint32_t)tapMv * (uint32_t)scale) / 1000u;
  return (v > 0xFFFFu) ? 0xFFFFu : (uint16_t)v;
}

uint8_t detectCells(uint16_t packMv) {
  if (packMv < kMinValidMv) return 0;
  return (uint8_t)(((uint32_t)packMv + kCellDetectMv - 1u) / kCellDetectMv);
}

uint8_t remainingPct(uint16_t packMv, uint8_t cells) {
  if (cells == 0) return 0;
  const int32_t perCell = (int32_t)packMv / (int32_t)cells;
  if (perCell <= (int32_t)kCellEmptyMv) return 0;
  if (perCell >= (int32_t)kCellFullMv) return 100;
  return (uint8_t)(((perCell - (int32_t)kCellEmptyMv) * 100) /
                   ((int32_t)kCellFullMv - (int32_t)kCellEmptyMv));
}

uint8_t CellLatch::update(uint16_t packMv) {
  const uint8_t n = detectCells(packMv);
  if (n == 0) { reset(); return 0; }
  if (n == candidate_) {
    if (run_ < kCellConfirmSamples) run_++;
  } else {
    candidate_ = n;
    run_ = 1;
  }
  return (run_ >= kCellConfirmSamples) ? candidate_ : 0;
}

void CellLatch::reset() {
  candidate_ = 0;
  run_ = 0;
}

}  // namespace vbat
