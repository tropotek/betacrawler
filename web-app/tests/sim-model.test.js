import test from 'node:test';
import assert from 'node:assert/strict';
import {
  SimModel, truncDiv, trianglePercent, deadbanded, mix, computeArmed, neutralUs,
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
  assert.deepEqual(mix(1600, 1500, 1500, 1000, 2000, 100, 100, 100, 0), [1600, 1600]);
});

test('mix: steer ratio scales the turn only', () => {
  assert.deepEqual(mix(1500, 2000, 1500, 1000, 2000, 100, 100, 50, 0), [1750, 1250]);
});

test('mix: reverse ratio scales reverse only', () => {
  assert.deepEqual(mix(1000, 1500, 1500, 1000, 2000, 100, 50, 100, 0), [1250, 1250]);
});

test('mix: clamps proportionally rather than saturating', () => {
  assert.deepEqual(mix(2000, 2000, 1500, 1000, 2000, 100, 100, 100, 0), [2000, 1500]);
});

test('mix: uses C truncation for the ratio scaling', () => {
  assert.deepEqual(mix(1499, 1500, 1500, 1000, 2000, 100, 30, 100, 0), [1500, 1500]);
});

test('computeArmed rules', () => {
  assert.equal(computeArmed(false, true, 0, 1700, 2000), false);
  assert.equal(computeArmed(true, true, 0, 1700, 2000), true);
  assert.equal(computeArmed(true, false, 1800, 1700, 2000), true);
  assert.equal(computeArmed(true, false, 1500, 1700, 2000), false);
});

test('neutralUs depends on direction', () => {
  assert.equal(neutralUs(1000, 2000, true), 1500);
  assert.equal(neutralUs(1000, 2000, false), 1000);
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
  assert.equal(mod.get('esc0.throttle_us'), 1500);
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
  mod.set('tank_drive.steer_ratio', 0, 0);
  assert.notEqual(mod.telemetry(0).drv_l, before);
});

test('esc holds neutral while the arm switch is inactive', () => {
  const tlm = makeModel().telemetry(0);
  assert.equal(tlm.arm0, ARM_ARMING);
  assert.equal(tlm.esc0, 1500);
});

test('esc arms after the hold once the arm source allows it', () => {
  const mod = makeModel();
  mod.set('tank_drive.arm_src', 'none', 0);
  mod.set('esc0.mode', 'armed', 0);
  let tlm;
  for (let t = 0; t <= 2000; t += 100) tlm = mod.telemetry(t);
  assert.equal(tlm.arm0, ARM_ARMED);
  assert.equal(tlm.esc0, 1500);
});

test('changing the esc rate demotes an armed esc', () => {
  const mod = makeModel();
  mod.set('tank_drive.arm_src', 'none', 0);
  mod.set('esc0.mode', 'armed', 0);
  for (let t = 0; t <= 3000; t += 100) mod.telemetry(t);
  assert.equal(mod.telemetry(3000).arm0, ARM_ARMED);
  mod.set('esc0.rate', '400', 3000);
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
