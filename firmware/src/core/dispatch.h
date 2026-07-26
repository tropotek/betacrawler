#pragma once
#include "core/registry.h"
#include "core/protocol.h"

namespace core {

// The persistence seam, so `save` is testable natively. (The hardware seam is
// now core::Module -- one per module, in core/module.h.)
struct Persistence {
  virtual ~Persistence() {}
  virtual bool save(const Params& p) = 0;
  virtual bool load(Params* p) = 0;
};

// Serializes one telemetry frame from values collected by the registry. The
// field set comes entirely from the registered modules' TlmDefs, so a board
// that enables a new sensor module publishes it with no change here.
size_t writeTelemetry(char* out, size_t cap, const Registry& reg, const TlmValue* vals);

class Dispatcher {
 public:
  Dispatcher(Registry& reg, Params& p, Persistence& store)
      : reg_(reg), p_(p), store_(store) {}

  // Writes a response line (no trailing newline) into out. Returns length.
  size_t handle(const Request& q, char* out, size_t cap);

  bool telemetryEnabled() const { return tlmOn_; }

 private:
  Registry&    reg_;
  Params&      p_;
  Persistence& store_;
  bool         tlmOn_ = true;
};

}  // namespace core
