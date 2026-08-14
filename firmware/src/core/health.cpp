#include "core/health.h"
#include "core/boot_log.h"
#include <stdio.h>

namespace core {

const char* faultName(Fault f) {
  switch (f) {
    case Fault::None:     return "none";
    case Fault::Registry: return "registry";
    case Fault::Panic:    return "panic";
  }
  return "unknown";
}

void Health::fail(Fault f) {
  if (f == Fault::None) return;
  if (fault_ != Fault::None) return;
  fault_ = f;
  char msg[BootLog::kMaxLen];
  snprintf(msg, sizeof(msg), "boot: fault=%s", faultName(f));
  bootLog().add(msg);
}

Health& health() {
  static Health h;
  return h;
}

}  // namespace core
