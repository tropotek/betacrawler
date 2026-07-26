#include "core/boot_log.h"
#include <string.h>

namespace core {

void BootLog::add(const char* msg) {
  if (!msg) return;
  if (count_ >= kMaxLines) { dropped_ = true; return; }
  strncpy(lines_[count_], msg, kMaxLen - 1);
  lines_[count_][kMaxLen - 1] = '\0';
  ++count_;
}

const char* BootLog::line(uint8_t i) const {
  return i < count_ ? lines_[i] : "";
}

void BootLog::clear() {
  count_ = 0;
  dropped_ = false;
}

BootLog& bootLog() {
  static BootLog instance;
  return instance;
}

}  // namespace core
