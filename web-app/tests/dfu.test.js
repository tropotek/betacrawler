import test from 'node:test';
import assert from 'node:assert/strict';
import { validateImage, parseMemoryLayout, sectorsFor, DfuError } from '../js/dfu.js';

// What an F411CE's ROM bootloader actually reports, verbatim from `dfu-util -l`
// -- two spaces before the slash, and `g` as the type letter.
const LAYOUT = '@Internal Flash  /0x08000000/04*016Kg,01*064Kg,03*128Kg';

// A real betacrawler image's first 8 bytes: initial SP at the very top of the
// F411's 128KB SRAM, then a Thumb reset vector in flash.
function fakeImage(len = 4096, msp = 0x20020000, reset = 0x08000201) {
  const bytes = new Uint8Array(len);
  const view = new DataView(bytes.buffer);
  view.setUint32(0, msp, true);
  view.setUint32(4, reset, true);
  return bytes;
}

test('validateImage accepts a stack pointer at the top of SRAM', () => {
  assert.doesNotThrow(() => validateImage(fakeImage()));
});

test('validateImage rejects an image that is too small', () => {
  assert.throws(() => validateImage(new Uint8Array(64)), /too small to be firmware/);
});

test('validateImage rejects an image larger than the flash', () => {
  assert.throws(() => validateImage(fakeImage(600 * 1024)), /larger than the 512KB flash/);
});

test('validateImage names firmware.elf as the likely mistake', () => {
  // ELF starts 0x7f 'E' 'L' 'F' -- a stack pointer nowhere near SRAM.
  assert.throws(() => validateImage(fakeImage(4096, 0x464c457f, 0x00010102)),
                /use firmware.bin instead/);
});

test('validateImage rejects a non-Thumb reset vector', () => {
  assert.throws(() => validateImage(fakeImage(4096, 0x20020000, 0x08000200)),
                /not a Thumb address in flash/);
});

test('validateImage throws DfuError', () => {
  assert.throws(() => validateImage(new Uint8Array(64)), DfuError);
});

test('validateImage reads a subarray at its own offset', () => {
  // A Uint8Array view into a larger buffer must not be read from byte 0 of
  // that buffer -- fetch().arrayBuffer() slices are routinely views.
  const backing = new Uint8Array(8192);
  backing.set(fakeImage(4096), 4096);
  assert.doesNotThrow(() => validateImage(backing.subarray(4096)));
});

test('parseMemoryLayout reads the STM32 bootloader descriptor', () => {
  const layout = parseMemoryLayout(LAYOUT);
  assert.equal(layout.length, 8);
  assert.deepEqual(layout[0], { start: 0x08000000, size: 16 * 1024 });
  assert.deepEqual(layout[3], { start: 0x0800c000, size: 16 * 1024 });
  assert.deepEqual(layout[4], { start: 0x08010000, size: 64 * 1024 });
  assert.deepEqual(layout[5], { start: 0x08020000, size: 128 * 1024 });
});

test('parseMemoryLayout falls back when the device gives no name', () => {
  // Chrome surfaces interfaceName as null on this bootloader, so this is the
  // path that actually runs.
  assert.deepEqual(parseMemoryLayout(null), parseMemoryLayout(undefined));
  assert.deepEqual(parseMemoryLayout(null), parseMemoryLayout(LAYOUT));
});

test('parseMemoryLayout falls back on a region that is not the flash', () => {
  assert.equal(parseMemoryLayout('@Device Feature/0xFFFF0000/01*004 e')[0].start, 0x08000000);
});

test('parseMemoryLayout falls back on a descriptor it cannot read', () => {
  assert.equal(parseMemoryLayout('@Internal Flash /0x08000000/nonsense')[0].start, 0x08000000);
});

test('sectorsFor covers only the sectors an image touches', () => {
  // 68KB spans the four 16KB sectors and the first byte of the 64KB one.
  const sectors = sectorsFor(parseMemoryLayout(LAYOUT), 0x08000000, 68 * 1024);
  assert.equal(sectors.length, 5);
  assert.equal(sectors.at(-1).size, 64 * 1024);
});

test('sectorsFor is exact at a sector boundary', () => {
  assert.equal(sectorsFor(parseMemoryLayout(LAYOUT), 0x08000000, 64 * 1024).length, 4);
});
