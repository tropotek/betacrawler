import test from 'node:test';
import assert from 'node:assert/strict';
import {
  SimModel, truncDiv, trianglePercent, deadbanded, mix, carMix, applyInvert, applyTrim,
  computeArmed, neutralUs,
  nextArmState, nextPulseUs, effectiveMaxUs, MODE_OFF, MODE_ARMED, MODE_INPUT,
  ARM_OFF, ARM_ARMING, ARM_ARMED, FRAME_US,
} from '../js/sim-model.js';
import { SIM_SCHEMA } from '../js/sim-schema.js';

function makeModel() { return new SimModel(SIM_SCHEMA.params); }

test('truncDiv truncates toward zero like C, unlike JS floor division', () => {
  assert.equal(truncDiv(-30, 100), 0);
  assert.equal(truncDiv(-49771, 100), -497);
  assert.equal(truncDiv(49771, 100), 497);
  assert.equal(truncDiv(-3300, 100), -33);
});

test('trianglePercent rises then falls', () => {
  assert.equal(trianglePercent(0, 4000), 0);
  assert.equal(trianglePercent(2000, 4000), 100);
  assert.equal(trianglePercent(1000, 4000), 50);
  assert.equal(trianglePercent(3000, 4000), 50);
});

test('deadbanded snaps to centre inside the band', () => {
  assert.equal(deadbanded(1510, 1500, 20), 1500);
  assert.equal(deadbanded(1530, 1500, 20), 1530);
  assert.equal(deadbanded(1500, 1500, 0), 1500);
});

test('mix: forward drives both tracks equally', () => {
  assert.deepEqual(mix(1600, 1500, 1500, 1000, 2000, 100, 100, 100, 0), [1600, 1600, 1500]);
});

test('mix: steer ratio scales the turn only', () => {
  assert.deepEqual(mix(1500, 2000, 1500, 1000, 2000, 100, 100, 50, 0), [1750, 1250, 1750]);
});

test('mix: reverse ratio scales reverse only', () => {
  assert.deepEqual(mix(1000, 1500, 1500, 1000, 2000, 100, 50, 100, 0), [1250, 1250, 1500]);
});

test('mix: clamps proportionally rather than saturating', () => {
  assert.deepEqual(mix(2000, 2000, 1500, 1000, 2000, 100, 100, 100, 0), [2000, 1500, 2000]);
});

test('mix: uses C truncation for the ratio scaling', () => {
  assert.deepEqual(mix(1499, 1500, 1500, 1000, 2000, 100, 30, 100, 0), [1500, 1500, 1500]);
});

test('computeArmed rules', () => {
  assert.equal(computeArmed(false, true, 0, 1700, 2000), false);
  assert.equal(computeArmed(true, true, 0, 1700, 2000), true);
  assert.equal(computeArmed(true, false, 1800, 1700, 2000), true);
  assert.equal(computeArmed(true, false, 1500, 1700, 2000), false);
});

test('neutralUs is always the midpoint of the calibrated span', () => {
  assert.equal(neutralUs(1000, 2000), 1500);
  assert.equal(neutralUs(1200, 2000), 1600);
  assert.equal(neutralUs(1500, 1500), 1500);
});

test('arm state promotes only after the hold with throttle low', () => {
  const s = nextArmState(ARM_OFF, false, true, 0, 0, 2000, true);
  assert.equal(s, ARM_ARMING);
  assert.equal(nextArmState(s, false, false, 1999, 0, 2000, true), ARM_ARMING);
  assert.equal(nextArmState(s, false, false, 2000, 0, 2000, true), ARM_ARMED);
  assert.equal(nextArmState(s, false, false, 5000, 0, 2000, false), ARM_ARMING);
  assert.equal(nextArmState(ARM_ARMED, true, false, 5000, 0, 2000, true), ARM_OFF);
});

test('pulse is neutral until armed, then follows the input', () => {
  assert.equal(nextPulseUs(ARM_ARMING, MODE_INPUT, 1000, 2000, 1500, 1800, false, 1500), 1500);
  assert.equal(nextPulseUs(ARM_ARMED, MODE_INPUT, 1000, 2000, 1500, 1800, false, 1500), 1800);
  assert.equal(nextPulseUs(ARM_ARMED, MODE_INPUT, 1000, 2000, 1500, 1800, true, 1500), 1500);
  assert.equal(nextPulseUs(ARM_ARMED, MODE_ARMED, 1000, 2000, 1700, 0, false, 1500), 1700);
});

test('effectiveMaxUs reserves low time inside the frame', () => {
  assert.equal(effectiveMaxUs(2000, FRAME_US['50']), 2000);
  assert.equal(effectiveMaxUs(2500, FRAME_US['400']), 2375);
});

test('values start at the schema defaults except rx.source', () => {
  const mod = makeModel();
  assert.equal(mod.get('motor0.throttle_us'), 1500);
  assert.equal(mod.get('device.name'), 'betacrawler');
  assert.equal(mod.get('rx.source'), 'sim');
});

test('telemetry covers every field the schema advertises', () => {
  const tlm = makeModel().telemetry(0);
  const expected = new Set(SIM_SCHEMA.tlm.map((t) => t.key));
  assert.deepEqual(new Set(Object.keys(tlm)), expected);
});

test('RC channels sweep and respect the protocol channel count', () => {
  const mod = makeModel();
  assert.equal(mod.telemetry(0).ch1, 988);
  assert.equal(mod.telemetry(2000).ch1, 2012);
  mod.set('rx.protocol', 'crossfire', 0);
  assert.equal(mod.telemetry(0).ch16, 0);
  assert.notEqual(mod.telemetry(0).ch12, 0);
});

test('selecting the uart source drops the link and the channels', () => {
  const mod = makeModel();
  mod.set('rx.source', 'uart', 0);
  const tlm = mod.telemetry(0);
  assert.equal(tlm.link, 0);
  assert.equal(tlm.rate, 0);
  assert.equal(tlm.ch1, 0);
  assert.equal(tlm.drv_l, 1500);
  assert.equal(tlm.drv_r, 1500);
});

test('drive outputs follow the mixer at a known instant', () => {
  const tlm = makeModel().telemetry(0);
  assert.equal(tlm.drv_l, 1009);
  assert.equal(tlm.drv_r, 1500);
});

test('steer ratio changes the drive outputs', () => {
  const mod = makeModel();
  const before = mod.telemetry(0).drv_l;
  mod.set('drive.steer_ratio', 0, 0);
  assert.notEqual(mod.telemetry(0).drv_l, before);
});

test('a motor drives nothing until its type is set', () => {
  // The safety default: an unconfigured board cannot know what is on the end
  // of the wire, so it emits no pulse at all. Neutral would be safe for an
  // ESC and 30% duty for an H-bridge.
  const tlm = makeModel().telemetry(0);
  assert.equal(tlm.arm0, ARM_OFF);
  assert.equal(tlm.motor0, 0);
  assert.equal(tlm.motor1, 0);
});

test('choosing a type starts the arm hold rather than driving at once', () => {
  const mod = makeModel();
  mod.set('motor0.type', 'brushed', 0);
  assert.equal(mod.telemetry(0).arm0, ARM_ARMING);
});

test('setting the mode off silences a configured motor again', () => {
  const mod = makeModel();
  mod.set('motor0.type', 'brushed', 0);
  mod.set('motor0.mode', 'off', 0);
  const tlm = mod.telemetry(0);
  assert.equal(tlm.arm0, ARM_OFF);
  assert.equal(tlm.motor0, 0);
});

test('a motor holds neutral while the arm switch is inactive', () => {
  const mod = makeModel();
  mod.set('motor0.type', 'brushed', 0);
  const tlm = mod.telemetry(0);
  assert.equal(tlm.arm0, ARM_ARMING);
  assert.equal(tlm.motor0, 1500);
});

test('esc arms after the hold once the arm source allows it', () => {
  const mod = makeModel();
  mod.set('drive.arm_src', 'none', 0);
  mod.set('motor0.type', 'brushless', 0);
  mod.set('motor0.mode', 'armed', 0);
  let tlm;
  for (let t = 0; t <= 2000; t += 100) tlm = mod.telemetry(t);
  assert.equal(tlm.arm0, ARM_ARMED);
  assert.equal(tlm.motor0, 1500);
});

test('changing the esc rate demotes an armed esc', () => {
  const mod = makeModel();
  mod.set('drive.arm_src', 'none', 0);
  mod.set('motor0.type', 'brushless', 0);
  mod.set('motor0.mode', 'armed', 0);
  for (let t = 0; t <= 3000; t += 100) mod.telemetry(t);
  assert.equal(mod.telemetry(3000).arm0, ARM_ARMED);
  mod.set('motor0.rate', '400', 3000);
  assert.equal(mod.telemetry(3000).arm0, ARM_ARMING);
});

test('save then revert reports flash and restores', () => {
  const mod = makeModel();
  mod.set('tlm.rate', 25, 0);
  mod.save();
  mod.set('tlm.rate', 40, 0);
  assert.equal(mod.revert(0), 'flash');
  assert.equal(mod.get('tlm.rate'), 25);
});

test('revert with nothing saved falls back to defaults', () => {
  const mod = makeModel();
  mod.set('tlm.rate', 40, 0);
  assert.equal(mod.revert(0), 'defaults');
  assert.equal(mod.get('tlm.rate'), 10);
});

test('loadDefaults resets every value', () => {
  const mod = makeModel();
  mod.set('tlm.rate', 40, 0);
  mod.loadDefaults(0);
  assert.equal(mod.get('tlm.rate'), 10);
});

test('carMix: throttle and steer pass through independently', () => {
  assert.deepEqual(carMix(1700, 1300, 1500, 1000, 2000, 100, 100, 100, 0), [1700, 1700, 1300]);
});

test('carMix: full steer at zero throttle leaves throttle at centre', () => {
  assert.deepEqual(carMix(1500, 2000, 1500, 1000, 2000, 100, 100, 100, 0), [1500, 1500, 2000]);
});

test('carMix: steer ratio scales steer only', () => {
  assert.deepEqual(carMix(1500, 2000, 1500, 1000, 2000, 100, 100, 50, 0), [1500, 1500, 1750]);
});

test('carMix: reverse ratio scales reverse only', () => {
  assert.deepEqual(carMix(1000, 1500, 1500, 1000, 2000, 100, 25, 100, 0), [1375, 1375, 1500]);
});

test('carMix: clamps to the output range', () => {
  assert.deepEqual(carMix(2500, 500, 1500, 1000, 2000, 100, 100, 100, 0), [2000, 2000, 1000]);
});

test('applyInvert mirrors about the calibrated midpoint', () => {
  assert.equal(applyInvert(1700, 1000, 2000, false), 1700);
  assert.equal(applyInvert(1700, 1000, 2000, true), 1300);
  assert.equal(applyInvert(1000, 1000, 2000, true), 2000);
  assert.equal(applyInvert(1500, 1000, 2500, true), 2000);
});

test('applyTrim offsets then clamps at the end stop', () => {
  assert.equal(applyTrim(1500, 60, 1000, 2000), 1560);
  assert.equal(applyTrim(1500, -60, 1000, 2000), 1440);
  assert.equal(applyTrim(1950, 200, 1000, 2000), 2000);
  assert.equal(applyTrim(1050, -200, 1000, 2000), 1000);
});

test('the servo applies invert and trim to the steering slot', () => {
  const mod = makeModel();
  mod.set('rx.source', 'sim', 0);
  mod.set('drive.mode', 'car', 0);
  const plain = mod.telemetry(1000).srv;
  const min = mod.num('servo.min_us');
  const max = mod.num('servo.max_us');

  mod.set('servo.invert', 'reversed', 0);
  assert.equal(mod.telemetry(1000).srv, min + max - plain);

  mod.set('servo.invert', 'normal', 0);
  mod.set('servo.trim_us', -120, 0);
  assert.equal(mod.telemetry(1000).srv, plain - 120);
});

test('car mode drives the bus without cross-coupling', () => {
  const mod = makeModel();
  mod.set('drive.mode', 'car', 0);
  mod.set('rx.source', 'sim', 0);
  // Sampled at 1000ms, where both synthetic channels sit inside the drive
  // range -- at t=0 they start at 988 and the mixer's clamp would mask the
  // pass-through this is checking.
  const t = mod.telemetry(1000);
  // ch2 is throttle by default, and in car mode BOTH motor slots carry it --
  // either motor pin drives whichever wheel is wired to it.
  assert.equal(t.drv_l, t.ch2);
  assert.equal(t.drv_r, t.ch2);
  assert.notEqual(t.ch1, t.ch2);   // steer really is a different value
});

test('the two mixers produce different buses from the same channels', () => {
  const skid = makeModel();
  skid.set('rx.source', 'sim', 0);
  const car = makeModel();
  car.set('rx.source', 'sim', 0);
  car.set('drive.mode', 'car', 0);

  const a = skid.telemetry(1500);
  const b = car.telemetry(1500);
  assert.equal(a.ch1, b.ch1);
  assert.equal(a.ch2, b.ch2);
  // Same sticks, different buses: skid folds steer into both tracks, car keeps
  // throttle and steer on their own slots.
  assert.notDeepEqual([a.drv_l, a.drv_r], [b.drv_l, b.drv_r]);
  assert.equal(b.drv_l, b.ch2);
  assert.equal(b.drv_r, b.ch2);
});

test('a car steers on stock settings, with no servo parameter to set', () => {
  // The firmware derives the servo from drive.mode, so picking the mixer is
  // the only step between a default board and a working car.
  const mod = makeModel();
  mod.set('rx.source', 'sim', 0);
  mod.set('drive.mode', 'car', 0);
  const t = mod.telemetry(1000);
  assert.equal(t.srv, t.ch1);          // ch1 is steer by default
  assert.notEqual(t.ch1, t.ch2);
  assert.equal(t.drv_l, t.ch2);        // and throttle still reaches the motors
});

test('the servo is detached in skid mode', () => {
  const mod = makeModel();
  mod.set('rx.source', 'sim', 0);
  const t = mod.telemetry(1000);
  assert.equal(t.srv, 0);
});
