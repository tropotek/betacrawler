// Bench-only demo firmware for the DRV8833 wiring in
// _notes/docs/research/brushed-tank-variant.md, section 5a. Not part of the
// module system -- exercises PA6/PA7 (motor0) and PB8/PB9 (motor1) directly
// with an automatic ramp since no RX is on this bench board. Built by the
// bench_hbridge_demo env only; the normal blackpill_f411ce env never
// compiles this file.

#include <Arduino.h>

namespace {

constexpr uint8_t kMotor0Fwd = PA6;
constexpr uint8_t kMotor0Rev = PA7;
constexpr uint8_t kMotor1Fwd = PB8;
constexpr uint8_t kMotor1Rev = PB9;

constexpr int kMaxDuty = 200;   // out of 255 -- leaves headroom, plenty to see motion
constexpr int kStep = 4;
constexpr uint16_t kStepDelayMs = 20;
constexpr uint16_t kHoldMs = 800;

// signedDuty > 0 drives fwd pin, < 0 drives rev pin, 0 coasts (both low).
void drive(uint8_t fwdPin, uint8_t revPin, int signedDuty) {
    if (signedDuty > 0) {
        analogWrite(revPin, 0);
        analogWrite(fwdPin, signedDuty);
    } else if (signedDuty < 0) {
        analogWrite(fwdPin, 0);
        analogWrite(revPin, -signedDuty);
    } else {
        analogWrite(fwdPin, 0);
        analogWrite(revPin, 0);
    }
}

// Ramps both channels between -kMaxDuty and +kMaxDuty, motor1 in the
// opposite direction to motor0 so it's obvious both channels are
// independently controlled.
void rampTo(int &current, int target) {
    int dir = (target > current) ? 1 : -1;
    while (current != target) {
        current += dir * kStep;
        if ((dir > 0 && current > target) || (dir < 0 && current < target)) {
            current = target;
        }
        drive(kMotor0Fwd, kMotor0Rev, current);
        drive(kMotor1Fwd, kMotor1Rev, -current);
        Serial.print("duty0=");
        Serial.print(current);
        Serial.print(" duty1=");
        Serial.println(-current);
        delay(kStepDelayMs);
    }
    delay(kHoldMs);
}

int gDuty = 0;

} // namespace

void setup() {
    Serial.begin(115200);
    pinMode(kMotor0Fwd, OUTPUT);
    pinMode(kMotor0Rev, OUTPUT);
    pinMode(kMotor1Fwd, OUTPUT);
    pinMode(kMotor1Rev, OUTPUT);
    analogWriteFrequency(20000); // above audible range, DRV8833 handles it fine
    drive(kMotor0Fwd, kMotor0Rev, 0);
    drive(kMotor1Fwd, kMotor1Rev, 0);
    delay(1000);
}

void loop() {
    rampTo(gDuty, kMaxDuty);   // motor0 full fwd, motor1 full rev
    rampTo(gDuty, 0);          // both coast
    rampTo(gDuty, -kMaxDuty);  // motor0 full rev, motor1 full fwd
    rampTo(gDuty, 0);          // both coast
}
