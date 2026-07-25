#pragma once
#include "core/params.h"
#include "core/protocol.h"

namespace core {

// The seam: core never touches GPIO, it announces changes through this.
struct HardwareSink {
  virtual ~HardwareSink() {}
  virtual void onParamChanged(ParamId id, const Params& p) = 0;
};

// The same principle applied to flash, so `save` is testable natively.
struct Persistence {
  virtual ~Persistence() {}
  virtual bool save(const Params& p) = 0;
  virtual bool load(Params* p) = 0;
};

struct Telemetry {
  uint32_t up;    // ms
  uint32_t clk;   // MHz
  float    temp;  // degC
  int32_t  vdd;   // mV
  int32_t  ram;   // free bytes
  uint8_t  btn;   // 0|1
};

size_t writeTelemetry(char* out, size_t cap, const Telemetry& t);

class Dispatcher {
 public:
  Dispatcher(Params& p, HardwareSink& sink, Persistence& store)
      : p_(p), sink_(sink), store_(store) {}

  // Writes a response line (no trailing newline) into out. Returns length.
  size_t handle(const Request& q, char* out, size_t cap);

  bool telemetryEnabled() const { return tlmOn_; }

 private:
  Params&       p_;
  HardwareSink& sink_;
  Persistence&  store_;
  bool          tlmOn_ = true;
};

}  // namespace core
