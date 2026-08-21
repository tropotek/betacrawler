#pragma once
#include <stdint.h>

namespace core {

// Pack measurements published by whichever module owns a voltage sense, for
// whichever module transmits them. Named for the quantity it carries, not for
// any module -- core/ never names a feature. Same one-producer pattern as
// core::Inputs: the producer holds a mutable reference from its constructor,
// consumers read through Registry::battery() and only ever get const.
class Battery {
 public:
  uint16_t milliVolts()   const { return mv_; }
  uint8_t  cells()        const { return cells_; }
  uint8_t  remainingPct() const { return pct_; }

  // 0 means "never published", which distinguishes a board with no producer
  // from one whose producer has gone stale.
  uint32_t lastFreshMs()  const { return freshMs_; }

  void set(uint16_t mv, uint8_t cells, uint8_t pct) {
    mv_ = mv; cells_ = cells; pct_ = pct;
  }
  void markFresh(uint32_t nowMs) { freshMs_ = nowMs; }

 private:
  uint16_t mv_      = 0;
  uint8_t  cells_   = 0;
  uint8_t  pct_     = 0;
  uint32_t freshMs_ = 0;
};

}  // namespace core
