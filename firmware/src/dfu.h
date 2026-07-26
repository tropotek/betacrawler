#pragma once
#include "config.h"
#include "core/dispatch.h"

// Reboot-to-DFU: the Arduino half of core::Bootloader, exactly as storage.h is
// the Arduino half of core::Persistence.
//
// Two pieces, and the split matters:
//
//   DfuTrigger  -- the seam core/ talks to. enterDfu() only ARMS the reboot;
//                  reboot() actually performs it, and main.cpp calls that only
//                  after the response has been flushed to the host.
//
//   initVariant -- defined in dfu.cpp. The Arduino core declares it weak and
//                  calls it from main() BEFORE setup(), which is before
//                  Serial.begin() brings USB up. That is the whole reason this
//                  works: the jump happens on a fresh boot with the USB
//                  peripheral untouched, instead of from a running
//                  application with an active USB stack -- which is the
//                  approach that eats days.
//
// Everything here compiles to nothing when FEATURE_DFU is 0.

namespace dfu {

class DfuTrigger : public core::Bootloader {
 public:
  bool supported() const override;
  bool enterDfu() override;

  // True once enterDfu() has been accepted. main.cpp polls this after writing
  // its response so the reset happens at a point where nothing is still
  // waiting to go out on the wire.
  bool pending() const { return pending_; }

  // Writes the magic to the RTC backup register and resets. Does not return.
  void reboot();

 private:
  bool pending_ = false;
};

}  // namespace dfu
