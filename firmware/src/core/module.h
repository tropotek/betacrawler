#pragma once
#include "core/params.h"

namespace core {

class Registry;

// --- telemetry descriptors --------------------------------------------------

enum class TlmType { U32, I32, F32 };

// Describes one telemetry field well enough that the UI can render it with no
// per-field JavaScript. `div`, `dec` and `fmt` are display-only: the wire
// always carries the device's native units (vdd in millivolts, uptime in
// milliseconds), and only the browser divides, rounds and formats. That keeps
// the value the firmware validates and the value the wire carries identical.
//
// `fmt` names a renderer for what a divisor and a decimal count cannot
// express -- currently just "hms", uptime milliseconds as HH:MM:SS. It is a
// name, not a format string: both renderers (this firmware's panel and the
// browser) have to implement it, and a printf-style template would let the
// firmware describe something the browser has no way to honour.
struct TlmDef {
  const char* key;     // "vdd"
  const char* label;   // "VDD"
  const char* unit;    // "V", nullptr when unitless
  TlmType     type;
  uint16_t    div;     // display divisor; 0 or 1 means none
  uint8_t     dec;     // display decimal places
  const char* fmt;     // named renderer, nullptr for a plain number
  const char* group;   // nullptr -> inherit the owning module's label
  // Declared range for renderers that draw a proportion rather than a number
  // (`fmt` "bar"). Display-only, like div/dec: the wire still carries the
  // device's own value, and a field that leaves these equal declares no range
  // and is serialized without them. core/ attaches no meaning to the numbers.
  int32_t     lo;
  int32_t     hi;
};

union TlmValue {
  uint32_t u;
  int32_t  i;
  float    f;
};

// --- modules ----------------------------------------------------------------

// The static half of a module: what it contributes to the parameter table,
// the telemetry frame and the schema. A plain POD with no virtuals and no
// Arduino dependency, so it lives in a pure <module>_params.cpp that the
// native test build compiles -- which is what lets `pio test -e native`
// assemble the real device's schema with no board attached.
struct ModuleDesc {
  const char*     id;      // "led" -- stable wire identifier, appears in hello's `mods`
  const char*     label;   // "LED" -- human heading, and the default group for its items
  const ParamDef* params;
  uint8_t         paramCount;
  const TlmDef*   tlm;
  uint8_t         tlmCount;
};

// The behaviour half: the driver that actually touches hardware. Lives in
// <module>_driver.cpp, compiled only for real targets.
//
// This is the hardware seam. It replaces the single global HardwareSink with
// one interface per module, so core/ still never touches a GPIO and "setting
// led.blink_hz produced exactly one hardware call" stays provable natively by
// registering a fake in place of the real driver.
//
// Every method has a do-nothing default: a module that only publishes
// telemetry overrides readTelemetry() alone, one that only owns settings
// overrides onParamChanged() alone.
struct Module {
  virtual ~Module() {}

  // Called by Registry::add(). Records where this module's parameters landed
  // in the global table so the driver can read its own values through
  // globalParam() without knowing, or caring, what else is registered.
  void bind(uint8_t paramBase) { paramBase_ = paramBase; }

  // Called once by Registry::begin(), on EVERY module, before ANY module's
  // begin(). The default is a no-op: only a module that must read other
  // modules' state -- a display, a logger -- needs it, and the modules that
  // came out of the original refactor override nothing.
  //
  // Access is deliberately const. An observer may look at the device; it may
  // not reconfigure it behind dispatch's back, which would bypass validation
  // and the change notification every other module relies on.
  //
  // Resolve keys here, once (findParam/findTlm), rather than per tick: the
  // answer cannot change afterwards, since the registry is populated at boot
  // and never modified.
  virtual void attach(const Registry& reg, const Params& p) { (void)reg; (void)p; }

  // Called once at boot, after every module is registered and attached.
  virtual void begin() {}

  // Called every loop iteration. Must not block -- a module that stalls here
  // stalls telemetry and the command loop with it.
  virtual void tick(uint32_t nowMs) {}

  // `local` indexes this module's own params array, not the global table, so
  // a module never has to know where it landed in the registry.
  virtual void onParamChanged(uint8_t local, const Params& p) {}

  // Fills this module's slice of the telemetry frame: `out[i]` corresponds to
  // this module's tlm[i]. The slice is exactly tlmCount long.
  virtual void readTelemetry(TlmValue* out) {}

  // Optional unsolicited push, polled once per loop() alongside tick().
  // Writes one complete JSON line (no trailing newline) into `out` and
  // returns its length, or 0 when this module has nothing to send right
  // now. The default never does -- only a module that produces a result
  // too slow or large for the telemetry frame overrides this. Unlike
  // readTelemetry(), callers must NOT assume it is called at any fixed rate.
  virtual size_t pollPush(char* out, size_t cap) { (void)out; (void)cap; return 0; }

 protected:
  // Translates one of this module's local parameter indices into the global
  // ParamId that Params is keyed by: p.num(globalParam(P_MODE)).
  ParamId globalParam(uint8_t local) const { return (ParamId)(paramBase_ + local); }

 private:
  uint8_t paramBase_ = 0;
};

}  // namespace core
