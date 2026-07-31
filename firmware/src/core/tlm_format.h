#pragma once
#include <stddef.h>
#include <stdint.h>
#include "core/module.h"

namespace core {

// Renders telemetry as text, for renderers that have no JavaScript to lean on
// (the on-device display). Pure: no Arduino, no I/O, so the native suite owns
// it -- same bargain led_curve.cpp struck for the LED fade.
//
// The rules are app.js's formatTelemetryValue(), on purpose: divide by the
// descriptor's `div` when it is non-zero, round to `dec` places, append the
// unit. A second renderer that rounded differently would disagree with the
// browser about the same frame.

// Writes "<value> <unit>" (unit omitted when null). Returns the length
// written excluding the NUL, always <= n-1; returns 0 and touches nothing if
// out is null or n is 0. Output is truncated rather than overflowed.
size_t formatTlm(const TlmDef& def, TlmValue v, char* out, size_t n);

// "HH:MM:SS" from milliseconds -- the one place the display overrides the
// descriptor, since `up` is a raw millisecond counter on the wire but a
// dashboard wants a clock. Hours are not clamped to two digits: millis()
// wraps at ~49.7 days and the real figure is more useful than a truncated one.
size_t formatUptime(uint32_t ms, char* out, size_t n);

// "a.b.c.d" from a u32 packed big-endian (a in the high byte). Same
// truncate-don't-overflow contract as formatTlm/formatUptime.
size_t formatIp(uint32_t packed, char* out, size_t n);

}  // namespace core
