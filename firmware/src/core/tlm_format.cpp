#include "core/tlm_format.h"
#include <stdio.h>
#include <string.h>

namespace core {

namespace {

// Deliberately no "%f" and no "%lld" anywhere in this file. newlib-nano --
// what the STM32 Arduino core links by default -- strips float and long-long
// support out of printf unless -u _printf_float / -u _printf_longlong is
// forced at link time. Both would be an invisible dependency of this file on
// a build flag somewhere else, so the arithmetic is done in 64-bit integers
// and only 32-bit pieces are ever handed to snprintf.

uint64_t pow10u(uint8_t n) {
  uint64_t p = 1;
  while (n--) p *= 10;
  return p;
}

const uint8_t kMaxDec = 6;   // keeps p10 and the fraction inside 32 bits

// value/div rendered with `dec` places, as a sign plus a magnitude scaled by
// 10^dec. Rounds half away from zero, matching JS toFixed() on the values the
// descriptors actually produce.
void scale(const TlmDef& def, TlmValue v, uint8_t dec, bool* neg, uint64_t* mag) {
  const uint64_t p10 = pow10u(dec);
  const uint32_t div = def.div ? def.div : 1;   // app.js: `def.div ? ... : value`

  if (def.type == TlmType::F32) {
    double d = (double)v.f * (double)p10 / (double)div;
    *neg = d < 0;
    if (*neg) d = -d;
    *mag = (uint64_t)(d + 0.5);
    return;
  }

  uint64_t n;
  if (def.type == TlmType::I32) {
    *neg = v.i < 0;
    // Negate through int64 so INT32_MIN cannot overflow on the way.
    n = *neg ? (uint64_t)(-(int64_t)v.i) : (uint64_t)v.i;
  } else {
    *neg = false;
    n = (uint64_t)v.u;
  }
  *mag = (n * p10 + div / 2) / div;
}

}  // namespace

size_t formatTlm(const TlmDef& def, TlmValue v, char* out, size_t n) {
  if (!out || n == 0) return 0;

  // Named renderers for formats that division and decimal places cannot express.
  if (def.fmt) {
    if (strcmp(def.fmt, "ip") == 0) {
      return formatIp(v.u, out, n);
    }
  }

  const uint8_t dec = def.dec > kMaxDec ? kMaxDec : def.dec;
  bool neg = false;
  uint64_t mag = 0;
  scale(def, v, dec, &neg, &mag);

  const uint64_t p10 = pow10u(dec);
  // whole <= the original value, so it always fits in 32 bits for u32/i32
  // inputs; frac < p10 <= 10^6.
  const uint32_t whole = (uint32_t)(mag / p10);
  const uint32_t frac  = (uint32_t)(mag % p10);

  // "-0.05" is why the sign is carried separately: whole is 0 there, so
  // printing the quotient alone would silently drop it.
  const char* sign = neg && (whole || frac) ? "-" : "";
  const char* unit = def.unit ? def.unit : "";
  const char* sp   = def.unit ? " " : "";

  int w;
  if (dec)
    w = snprintf(out, n, "%s%lu.%0*lu%s%s", sign, (unsigned long)whole,
                 (int)dec, (unsigned long)frac, sp, unit);
  else
    w = snprintf(out, n, "%s%lu%s%s", sign, (unsigned long)whole, sp, unit);

  if (w < 0) { out[0] = '\0'; return 0; }
  return (size_t)w < n ? (size_t)w : n - 1;   // snprintf truncates and terminates
}

size_t formatUptime(uint32_t ms, char* out, size_t n) {
  if (!out || n == 0) return 0;
  const uint32_t s = ms / 1000u;
  int w = snprintf(out, n, "%02lu:%02lu:%02lu", (unsigned long)(s / 3600u),
                   (unsigned long)((s / 60u) % 60u), (unsigned long)(s % 60u));
  if (w < 0) { out[0] = '\0'; return 0; }
  return (size_t)w < n ? (size_t)w : n - 1;
}

size_t formatIp(uint32_t packed, char* out, size_t n) {
  if (!out || n == 0) return 0;
  int written = snprintf(out, n, "%u.%u.%u.%u",
                          (unsigned)(packed >> 24) & 0xFF, (unsigned)(packed >> 16) & 0xFF,
                          (unsigned)(packed >> 8) & 0xFF,  (unsigned)packed & 0xFF);
  if (written < 0) return 0;
  size_t len = (size_t)written;
  return len < n ? len : n - 1;
}

}  // namespace core
