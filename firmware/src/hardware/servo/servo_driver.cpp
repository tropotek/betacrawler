#include "hardware/servo/servo_driver.h"
#include "core/registry.h"
#include "config.h"

// The body (not just the class) must be guarded: PlatformIO compiles every
// .cpp under src/ as its own translation unit no matter what includes it, so
// an unguarded file here would still demand SERVO_PIN/SERVO_TIMER -- and drag
// in the STM32-only <HardwareTimer.h> -- on any board that never defines
// them. Same trap wifi_driver.cpp documents its own guard against, first hit
// for real by esp32_wroom32 (FEATURE_SERVO off, no servo hardware on a bare
// WROOM-32 dev board, and no ESP32 timer-PWM counterpart written for this
// module).
#if FEATURE_SERVO

#include <Arduino.h>
#include <HardwareTimer.h>
#include <new>

#ifndef SERVO_PIN
#error "FEATURE_SERVO is on but the board header defines no SERVO_PIN"
#endif
#ifndef SERVO_TIMER
#error "FEATURE_SERVO is on but the board header defines no SERVO_TIMER"
#endif
#if !FEATURE_DRIVE
#error "FEATURE_SERVO requires FEATURE_DRIVE -- the servo follows the drive mixer's steering slot"
#endif

// 50Hz frame. Overridable from a board header for a digital servo that wants
// a faster one; analogue servos expect 20ms.
#ifndef SERVO_FRAME_US
#define SERVO_FRAME_US 20000
#endif

namespace servo {

// Slots on the drive output bus: 2 is the steering output, 4 the vehicle
// layout (1 car, 0 skid). Slot numbers, not a dependency on the drive module
// -- the same convention motor0/motor1 use for the ARM slot.
constexpr uint8_t kSteerSlot = 2;
constexpr uint8_t kModeSlot  = 4;

// Storage for the one HardwareTimer, placement-new'd in begin().
//
// NOT `new`: this firmware allocates nothing on the heap (see config.h), and
// this is the module the motor and receiver modules will be copied from, so the
// precedent would cost more than the allocation.
//
// NOT a file-scope `static HardwareTimer` either -- the display driver's
// statics are safe because they "only record pins", whereas HardwareTimer's
// constructor enables the timer clock and calls into the HAL. At static-init
// time that would run before HAL_Init() and the clock configuration.
alignas(HardwareTimer) static uint8_t s_timerMem[sizeof(HardwareTimer)];

void ServoDriver::begin() {
  timer_ = new (s_timerMem) HardwareTimer(SERVO_TIMER);
  // The timer instance is named by the board header (explicit and greppable,
  // which matters when the next modules also want timers); only the channel
  // is derived from the pin, being a pure lookup with nothing to construct.
  ch_ = STM_PIN_CHANNEL(pinmap_function(digitalPinToPinName(SERVO_PIN), PinMap_PWM));
  timer_->setOverflow(SERVO_FRAME_US, MICROSEC_FORMAT);
  timer_->resume();
  detach();   // boot silent; tick() attaches once the drive bus reports car
}

void ServoDriver::attachOutput() {
  // setMode reclaims the pin for the timer's alternate function, which
  // detach() gave back to the GPIO peripheral.
  timer_->setMode(ch_, TIMER_OUTPUT_COMPARE_PWM1, SERVO_PIN);
  timer_->resumeChannel(ch_);
}

void ServoDriver::detach() {
  // A real detach, not a zero-width pulse: the servo relaxes and stops drawing
  // holding current, which matters when the whole board runs off USB.
  timer_->pauseChannel(ch_);
  pinMode(SERVO_PIN, OUTPUT);
  digitalWrite(SERVO_PIN, LOW);
  lastUs_ = 0;
}

// Invert then trim then clamp, on every path to the pin: both describe the
// linkage, so every route to the output gets them.
void ServoDriver::writeUs(uint16_t us) {
  const uint16_t out = applyTrim(applyInvert(us, minUs_, maxUs_, inverted_),
                                 trimUs_, minUs_, maxUs_);
  timer_->setCaptureCompare(ch_, out, MICROSEC_COMPARE_FORMAT);
  lastUs_ = out;
}

void ServoDriver::attach(const core::Registry& reg, const core::Params& p) {
  (void)p;
  driveInputs_ = &reg.driveOutputs();
}

// Calibration only -- tick() owns whether the output is driven at all.
void ServoDriver::apply(const core::Params& p) {
  minUs_    = (uint16_t)p.num(globalParam(P_MIN_US));
  maxUs_    = (uint16_t)p.num(globalParam(P_MAX_US));
  inverted_ = (p.num(globalParam(P_INVERT)) == INVERT_REVERSED);
  trimUs_   = p.num(globalParam(P_TRIM_US));
}

void ServoDriver::onParamChanged(uint8_t local, const core::Params& p) {
  // Every parameter feeds the same recompute -- the calibration values are
  // meaningless apart. globalParam() maps this module's own indices onto
  // wherever the registry placed them, so enabling another module never
  // breaks this.
  (void)local;
  apply(p);
}

void ServoDriver::tick(uint32_t nowMs) {
  (void)nowMs;
  // A steering servo exists only on a car, so the drive bus's layout slot --
  // not a parameter of this module's own -- decides whether the pin is driven.
  const bool wantAttached = (driveInputs_->get(kModeSlot) == 1);
  if (wantAttached != attached_) {
    if (wantAttached) attachOutput(); else detach();
    attached_ = wantAttached;
  }
  if (!attached_) return;

  const int16_t v = driveInputs_->get(kSteerSlot);
  // 0 is the bus's "this slot carries no data" sentinel, published by the
  // mixer on a stale link. Hold the last pulse rather than actuating to
  // min_us.
  if (v > 0) writeUs(clampUs(v, minUs_, maxUs_));
}

void ServoDriver::readTelemetry(core::TlmValue* out) {
  out[T_US].u = lastUs_;
}

}  // namespace servo

#endif  // FEATURE_SERVO
