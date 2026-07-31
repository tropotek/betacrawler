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
#include "core/registry.h"
#include "core/inputs.h"
#include "core/device_params.h"
#include "hardware/system/system_params.h"

// The shared control-signal bus (core/inputs.h). Constructed unconditionally
// -- even a board with FEATURE_SERVO but not FEATURE_RX gets a valid,
// all-zero bus this way, and the one module allowed to write it (rx, wired
// below) can be constructed against a real instance regardless of what
// other modules are enabled.
static core::Inputs g_inputs;

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

#if FEATURE_LED
#  include "features/led/led_params.h"
#  if FW_TARGET_ARDUINO
#    include "features/led/led_driver.h"
     static led::LedDriver g_led;
#    define LED_DRV (&g_led)
#  else
#    define LED_DRV nullptr
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

#if FEATURE_ESC
#  include "hardware/esc/esc_params.h"
#  if FW_TARGET_ARDUINO
#    include "hardware/esc/esc_driver.h"
     static esc::EscDriver g_esc;
#    define ESC_DRV (&g_esc)
#  else
#    define ESC_DRV nullptr
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

#if FEATURE_WIFI
#  include "hardware/wifi/wifi_params.h"
#  if FW_TARGET_ARDUINO
#    include "hardware/wifi/wifi_driver.h"
     static wifi::WifiDriver g_wifi;
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
  reg.add(device::kDesc);            // always: device.name, tlm.rate
  reg.add(sys::kDesc, SYSTEM_DRV);   // always: uptime/clock/ram/temp/vdd
#if FEATURE_BUTTON
  reg.add(button::kDesc, BUTTON_DRV);
#endif
#if FEATURE_LED
  reg.add(led::kDesc, LED_DRV);
#endif
#if FEATURE_SERVO
  reg.add(servo::kDesc, SERVO_DRV);
#endif
#if FEATURE_ESC
  reg.add(esc::kDesc, ESC_DRV);
#endif
#if FEATURE_RX
  reg.add(rx::kDesc, RX_DRV);
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
