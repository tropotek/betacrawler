// The module wiring point. This is the ONLY file that knows the full list of
// modules, and adding one means adding a folder plus one #if block here.
//
// Compiled by BOTH environments, deliberately:
//
//   * on a real target (FW_TARGET_ARDUINO) each enabled module registers its
//     descriptor together with a driver instance;
//   * natively, the same list registers the same descriptors with a null
//     driver, because the *_driver.cpp files are excluded from the native
//     build by platformio.ini's build_src_filter.
//
// One list, two builds. That is what lets `pio test -e native` assemble the
// real device's parameter and telemetry tables -- and so keep
// test/golden/schema.json honest -- with no board attached and no second
// hand-maintained copy of the list to drift out of sync.
//
// The DRV macro pattern below is the whole trick: a module's driver header and
// static instance only exist in the Arduino build.

#include "config.h"
#include "core/battery.h"
#include "core/registry.h"
#include "core/inputs.h"
#include "core/device_params.h"
#include "hardware/system/system_params.h"

// The shared control-signal bus (core/inputs.h). Constructed unconditionally
// -- even a board with FEATURE_SERVO but not FEATURE_RX gets a valid,
// all-zero bus this way, and the one module allowed to write it (rx, wired
// below) can be constructed against a real instance regardless of what
// other modules are enabled.
// Third bus, same one-producer pattern as g_inputs and g_driveOutputs:
// vbat writes it, rx reads it.
static core::Battery g_battery;

static core::Inputs g_inputs;

// tank_drive's own bus (core/inputs.h) -- a second, parallel application of
// the same one-producer pattern g_inputs/rx already establishes above, not
// a fact specific to rx. See docs/architecture.md's "Inputs bus" section.
static core::Inputs g_driveOutputs;

#if FW_TARGET_ARDUINO
#  include "hardware/system/system_driver.h"
   static sys::SystemDriver g_system;
#  define SYSTEM_DRV (&g_system)
#else
#  define SYSTEM_DRV nullptr
#endif

#if FEATURE_BUTTON
#  include "hardware/button/button_params.h"
#  if FW_TARGET_ARDUINO
#    include "hardware/button/button_driver.h"
     static button::ButtonDriver g_button;
#    define BUTTON_DRV (&g_button)
#  else
#    define BUTTON_DRV nullptr
#  endif
#endif

#if FEATURE_SERVO
#  include "hardware/servo/servo_params.h"
#  if FW_TARGET_ARDUINO
#    include "hardware/servo/servo_driver.h"
     static servo::ServoDriver g_servo;
#    define SERVO_DRV (&g_servo)
#  else
#    define SERVO_DRV nullptr
#  endif
#endif

#if FEATURE_RX
#  include "hardware/rx/rx_params.h"
#  if FW_TARGET_ARDUINO
#    include "hardware/rx/rx_driver.h"
     static rx::RxDriver g_rx(g_inputs);
#    define RX_DRV (&g_rx)
#  else
#    define RX_DRV nullptr
#  endif
#endif

#if FEATURE_TANK_DRIVE
#  include "features/tank_drive/tank_drive_params.h"
#  if FW_TARGET_ARDUINO
#    include "features/tank_drive/tank_drive_driver.h"
     static tank_drive::TankDriveDriver g_tankDrive(g_driveOutputs);
#    define TANK_DRIVE_DRV (&g_tankDrive)
#  else
#    define TANK_DRIVE_DRV nullptr
#  endif
#endif

#if FEATURE_VBAT
#  include "hardware/vbat/vbat_params.h"
#  if FW_TARGET_ARDUINO
#    include "hardware/vbat/vbat_driver.h"
     static vbat::VbatDriver g_vbat(g_battery);
#    define VBAT_DRV (&g_vbat)
#  else
#    define VBAT_DRV nullptr
#  endif
#endif

#if FEATURE_ESC0
#  include "hardware/esc0/esc0_params.h"
#  if FW_TARGET_ARDUINO
#    include "hardware/esc0/esc0_driver.h"
     static esc0::EscDriver g_esc0;
#    define ESC0_DRV (&g_esc0)
#  else
#    define ESC0_DRV nullptr
#  endif
#endif

#if FEATURE_ESC1
#  include "hardware/esc1/esc1_params.h"
#  if FW_TARGET_ARDUINO
#    include "hardware/esc1/esc1_driver.h"
     static esc1::EscDriver g_esc1;
#    define ESC1_DRV (&g_esc1)
#  else
#    define ESC1_DRV nullptr
#  endif
#endif

#if FEATURE_WIFI
#  include "hardware/wifi/wifi_params.h"
#  if FW_TARGET_ARDUINO
#    if FW_MCU_ESP32
#      include "hardware/wifi/wifi_esp32_driver.h"
       static wifi::WifiEsp32Driver g_wifi;
#    else
#      include "hardware/wifi/wifi_driver.h"
       static wifi::WifiDriver g_wifi;
#    endif
#    define WIFI_DRV (&g_wifi)
#  else
#    define WIFI_DRV nullptr
#  endif
#endif

#if FEATURE_ST7789_240X240
#  include "hardware/st7789_240x240/st7789_240x240_params.h"
#  if FW_TARGET_ARDUINO
#    include "hardware/st7789_240x240/st7789_240x240_driver.h"
     static st7789::St7789Driver g_display;
#    define DISPLAY_DRV (&g_display)
#  else
#    define DISPLAY_DRV nullptr
#  endif
#endif

namespace core {

// Registration order fixes the order of the schema, the telemetry frame and
// the flash layout. Changing it changes Registry::fingerprint(), which
// invalidates saved settings -- by design, not by accident.
void registerModules(Registry& reg) {
  reg.setInputs(g_inputs);
  reg.setDriveOutputs(g_driveOutputs);
  reg.setBattery(g_battery);
  reg.add(device::kDesc);            // always: device.name, tlm.rate
  reg.add(sys::kDesc, SYSTEM_DRV);   // always: uptime/clock/ram/temp/vdd
#if FEATURE_BUTTON
  reg.add(button::kDesc, BUTTON_DRV);
#endif
#if FEATURE_SERVO
  reg.add(servo::kDesc, SERVO_DRV);
#endif
  // Before rx: Registry::tick() walks in registration order, so measuring
  // here means rx transmits a value produced in the same loop pass.
#if FEATURE_VBAT
  reg.add(vbat::kDesc, VBAT_DRV);
#endif
#if FEATURE_RX
  reg.add(rx::kDesc, RX_DRV);
#endif
  // tank_drive must register after rx and before esc0/esc1: Registry::tick()
  // walks modules in registration order, and tank_drive must mix each
  // loop's freshly-decoded rx frame before either ESC reads it that same
  // loop. This is the first place in this codebase where registration order
  // is a correctness requirement, not just a schema/telemetry/flash-layout
  // ordering choice -- do not reorder these three without re-reading
  // _notes/docs/plans/2026-08-14-tank-drive-mixer-design.md's "Firmware
  // source changes" section first.
#if FEATURE_TANK_DRIVE
  reg.add(tank_drive::kDesc, TANK_DRIVE_DRV);
#endif
#if FEATURE_ESC0
  reg.add(esc0::kDesc, ESC0_DRV);
#endif
#if FEATURE_ESC1
  reg.add(esc1::kDesc, ESC1_DRV);
#endif
#if FEATURE_WIFI
  reg.add(wifi::kDesc, WIFI_DRV);
#endif
  // Last on purpose: the display observes the modules above it, and
  // Registry::begin() attaches every module before beginning any, so
  // registration order does not affect what it can read -- but reading the
  // list in the order things appear on screen is easier to follow.
#if FEATURE_ST7789_240X240
  reg.add(st7789::kDesc, DISPLAY_DRV);
#endif
}

// Exposes the one wifi driver instance's WifiScanner interface to main.cpp,
// which cannot otherwise reach a static living in this translation unit.
// nullptr on any build without FEATURE_WIFI (or without FW_TARGET_ARDUINO,
// i.e. the native test build) -- Dispatcher::setWifiScanner() already
// treats a null seam as "this firmware cannot scan," exactly like an absent
// Bootloader already does for `dfu`.
WifiScanner* wifiScanner() {
#if FEATURE_WIFI && FW_TARGET_ARDUINO
  return &g_wifi;
#else
  return nullptr;
#endif
}

}  // namespace core
