#pragma once
#include "core/params.h"

namespace core {

// --- telemetry descriptors --------------------------------------------------

enum class TlmType { U32, I32, F32 };

// Describes one telemetry field well enough that the UI can render it with no
// per-field JavaScript. `div` and `dec` are display-only: the wire always
// carries the device's native units (vdd in millivolts), and only the browser
// divides and rounds. That keeps the value the firmware validates and the
// value the wire carries identical.
struct TlmDef {
  const char* key;     // "vdd"
  const char* label;   // "VDD"
  const char* unit;    // "V", nullptr when unitless
  TlmType     type;
  uint16_t    div;     // display divisor; 0 or 1 means none
  uint8_t     dec;     // display decimal places
  const char* group;   // nullptr -> inherit the owning module's label
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

  // Called once at boot, after every module is registered.
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

 protected:
  // Translates one of this module's local parameter indices into the global
  // ParamId that Params is keyed by: p.num(globalParam(P_MODE)).
  ParamId globalParam(uint8_t local) const { return (ParamId)(paramBase_ + local); }

 private:
  uint8_t paramBase_ = 0;
};

}  // namespace core
