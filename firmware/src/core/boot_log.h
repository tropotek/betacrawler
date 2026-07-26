#pragma once
#include <stdint.h>
#include <stddef.h>

namespace core {

// Boot-time health lines, kept so a host that connects AFTER boot can still
// read them.
//
// This exists because of a measured property of the link, not a hypothetical:
// USB CDC enumerates well after setup() has run, and the app connects later
// still, so anything printed during boot reaches a serial monitor only if one
// happened to be attached at the time. Holding the lines lets `hello` replay
// them, which is what puts boot health in the app's Terminal on a reboot.
//
// Fixed-size like everything else here -- no allocation. Overflowing is
// reported rather than hidden, because this buffer is the only record of what
// happened at boot.
class BootLog {
 public:
  static const uint8_t kMaxLines = 8;
  static const uint8_t kMaxLen   = 96;

  void add(const char* msg);          // truncates; drops once full
  uint8_t     count() const { return count_; }
  const char* line(uint8_t i) const;  // "" when out of range
  bool        dropped() const { return dropped_; }
  void        clear();

 private:
  char    lines_[kMaxLines][kMaxLen] = {};
  uint8_t count_   = 0;
  bool    dropped_ = false;
};

// The instance main.cpp and the drivers share. Deliberately a singleton: a
// boot diagnostic has exactly one destination, and threading a reference
// through every module's attach() would be a lot of plumbing to carry one
// string per module. Tests construct their own BootLog instead.
BootLog& bootLog();

}  // namespace core
