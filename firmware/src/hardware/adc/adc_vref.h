#pragma once
#include <stdint.h>

// The true analog supply, derived from the MCU's internal reference against
// its factory calibration constant. Shared: any module converting an ADC
// count to millivolts needs it, and a second copy of this routine would be a
// second chance to get the calibration address or the sample time wrong.
//
// Not in core/ -- it calls analogRead().
namespace adcref {

int32_t vddaMv();

}  // namespace adcref
