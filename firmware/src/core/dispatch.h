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

// The reboot-to-bootloader seam, exactly parallel to Persistence above: an
// effect `core/` must be able to request but must not implement. src/dfu.cpp
// supplies the Arduino half, the way storage.cpp does for Persistence. It is
// not a Module because it has no parameters and no telemetry -- the registry
// would buy it nothing.
//
// enterDfu() ARMS the reboot; it must not reset the MCU itself. The response
// to the `dfu` op has to reach the host before the port disappears, or the app
// cannot tell a successful reboot from a board that died. main.cpp prints the
// reply, flushes, and only then performs the reset.
struct Bootloader {
  virtual ~Bootloader() {}
  virtual bool supported() const = 0;   // reported in hello's `caps`
  virtual bool enterDfu() = 0;          // false if this board cannot
};

// The SSID-scan seam, parallel to Bootloader above: an effect core/ must be
// able to request but must not implement (the real AT+CWLAP exchange lives
// in wifi_driver.cpp, Arduino-only). startScan() only ARMS the scan --
// Dispatcher::handle() must never block, and a multi-second AT exchange
// cannot run inside it. The result reaches the host later, through
// Module::pollPush(), not through this interface.
struct WifiScanner {
  virtual ~WifiScanner() {}
  virtual bool startScan() = 0;   // false if already scanning
};

// Serializes one telemetry frame from values collected by the registry. The
// field set comes entirely from the registered modules' TlmDefs, so a board
// that enables a new sensor module publishes it with no change here.
size_t writeTelemetry(char* out, size_t cap, const Registry& reg, const TlmValue* vals);

// Serializes an unsolicited log line, `{"log":"..."}`. Id-less like telemetry,
// which is what makes the backend route it as a device log
// (app/backend/protocol.py's is_log, main.py's WS "log" message).
//
// Exists so a driver never hand-rolls JSON: `msg` is escaped properly, and a
// message too long for `cap` yields nothing rather than a truncated line the
// host could not parse. Returns the length written, 0 on refusal.
size_t writeLog(char* out, size_t cap, const char* msg);

class Dispatcher {
 public:
  // `boot` is optional and defaults to none: a board built without DFU
  // support genuinely has no bootloader seam, the same way Registry::add()
  // accepts a null driver for a module with no hardware half.
  Dispatcher(Registry& reg, Params& p, Persistence& store,
             Bootloader* boot = nullptr)
      : reg_(reg), p_(p), store_(store), boot_(boot) {}

  // Writes a response line (no trailing newline) into out. Returns length.
  size_t handle(const Request& q, char* out, size_t cap);

  bool telemetryEnabled() const { return tlmOn_; }

  // Wired separately from the constructor, not as another optional
  // constructor argument: unlike Bootloader (a single global singleton
  // constructed before registerModules() runs), the wifi driver instance
  // this points at is a static inside modules.cpp and only exists once
  // registerModules() has run -- see main.cpp's setup().
  void setWifiScanner(WifiScanner* s) { wifi_ = s; }

 private:
  Registry&    reg_;
  Params&      p_;
  Persistence& store_;
  Bootloader*  boot_;
  WifiScanner* wifi_ = nullptr;
  bool         tlmOn_ = true;
};

}  // namespace core
