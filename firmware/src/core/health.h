#pragma once
#include <stdint.h>

namespace core {

// The firmware's one health verdict. A singleton for the same reason BootLog
// is one: exactly one destination, and no plumbing through every module.
enum class Fault : uint8_t { None = 0, Registry = 1, Panic = 2 };

class Health {
 public:
  // First fault wins -- when one fault cascades into another, the root cause
  // is the actionable one. Also appends one boot-log line naming the fault.
  void  fail(Fault f);
  Fault fault() const { return fault_; }
  bool  ok() const { return fault_ == Fault::None; }
  void  reset() { fault_ = Fault::None; }

 private:
  Fault fault_ = Fault::None;
};

// Human name for a fault code, for the boot log. "none" for Fault::None.
const char* faultName(Fault f);

Health& health();

}  // namespace core
