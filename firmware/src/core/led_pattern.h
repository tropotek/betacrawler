#pragma once
#include <stdint.h>

namespace core {

// A looped on/off sequence: durations in ms, alternating, starting on.
// {950, 50} reads as solid with a wink; {100, 100} is an even 5Hz pulse.
struct Pattern {
  const uint16_t* stepsMs;
  uint8_t         count;
};

// True when the pattern is lit at elapsedMs. Wraps internally, so callers
// pass raw elapsed time. False for an empty or null pattern.
bool patternState(const Pattern& p, uint32_t elapsedMs);

}  // namespace core
