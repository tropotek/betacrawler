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
#include "core/device_params.h"
#include "hardware/system/system_params.h"

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

#if FEATURE_DISPLAY
#  include "hardware/display/display_params.h"
#  if FW_TARGET_ARDUINO
#    include "hardware/display/display_driver.h"
     static display::DisplayDriver g_display;
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
  reg.add(device::kDesc);            // always: device.name, tlm.rate
  reg.add(sys::kDesc, SYSTEM_DRV);   // always: uptime/clock/ram/temp/vdd
#if FEATURE_BUTTON
  reg.add(button::kDesc, BUTTON_DRV);
#endif
#if FEATURE_LED
  reg.add(led::kDesc, LED_DRV);
#endif
  // Last on purpose: the display observes the modules above it, and
  // Registry::begin() attaches every module before beginning any, so
  // registration order does not affect what it can read -- but reading the
  // list in the order things appear on screen is easier to follow.
#if FEATURE_DISPLAY
  reg.add(display::kDesc, DISPLAY_DRV);
#endif
}

}  // namespace core
