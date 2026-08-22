import test from 'node:test';
import assert from 'node:assert/strict';
import { validateImage, parseMemoryLayout, sectorsFor, flash, DfuError } from '../js/dfu.js';

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

// --- the flash sequence -------------------------------------------------------

const DNLOAD = 1, GETSTATUS = 3, CLRSTATUS = 4;
const STATE_DFU_IDLE = 2, STATE_DNBUSY = 4, STATE_DNLOAD_IDLE = 5, STATE_ERROR = 10;

// Records every control transfer and answers GETSTATUS from a scripted queue,
// so a test can make the device report busy, then idle, then an error.
// interfaceName defaults to null, which is what Chrome reports for this
// bootloader.
function makeFakeUsb({ interfaceName = null, statuses = null, failLeave = false } = {}) {
  const calls = [];
  let closed = false;
  const nextStatus = () => ((statuses && statuses.length)
    ? statuses.shift()
    : { status: 0, poll: 0, state: STATE_DNLOAD_IDLE });
  return {
    calls,
    get closed() { return closed; },
    configuration: {
      interfaces: [{
        interfaceNumber: 0,
        alternates: [{ alternateSetting: 0, interfaceName }],
      }],
    },
    open: async () => { calls.push(['open']); },
    selectConfiguration: async (c) => { calls.push(['selectConfiguration', c]); },
    claimInterface: async (i) => { calls.push(['claimInterface', i]); },
    selectAlternateInterface: async (i, a) => { calls.push(['selectAlternateInterface', i, a]); },
    close: async () => { closed = true; },
    controlTransferOut: async (setup, data) => {
      calls.push(['out', setup.request, setup.value, data ? [...new Uint8Array(data)] : []]);
      if (failLeave && setup.request === DNLOAD && data && data.byteLength === 0) {
        throw new Error('device detached');
      }
      return { status: 'ok', bytesWritten: data ? data.byteLength : 0 };
    },
    controlTransferIn: async (setup) => {
      calls.push(['in', setup.request]);
      if (setup.request !== GETSTATUS) {
        return { status: 'ok', data: new DataView(new ArrayBuffer(1)) };
      }
      const s = nextStatus();
      const buf = new Uint8Array([s.status, s.poll & 0xff, (s.poll >> 8) & 0xff,
                                  (s.poll >> 16) & 0xff, s.state, 0]);
      return { status: 'ok', data: new DataView(buf.buffer) };
    },
  };
}

const image = (len) => new Uint8Array(len).fill(0xa5);
const erases = (dev) => dev.calls.filter((c) => c[0] === 'out' && c[3][0] === 0x41);

test('flash claims interface 0 alt 0 before writing', async () => {
  const dev = makeFakeUsb();
  await flash(dev, image(2048));
  assert.deepEqual(dev.calls[0], ['open']);
  assert.ok(dev.calls.some((c) => c[0] === 'claimInterface' && c[1] === 0));
  assert.ok(dev.calls.some((c) => c[0] === 'selectAlternateInterface' && c[1] === 0 && c[2] === 0));
});

test('flash erases exactly the sectors the image covers', async () => {
  const dev = makeFakeUsb();
  await flash(dev, image(20 * 1024));   // spans two 16KB sectors
  assert.equal(erases(dev).length, 2);
  assert.deepEqual(erases(dev)[0][3], [0x41, 0x00, 0x00, 0x00, 0x08]);
  assert.deepEqual(erases(dev)[1][3], [0x41, 0x00, 0x40, 0x00, 0x08]);
});

test('flash erases the same sectors whether or not the layout is readable', async () => {
  const withName = makeFakeUsb({ interfaceName: LAYOUT });
  const withoutName = makeFakeUsb();
  await flash(withName, image(68 * 1024));
  await flash(withoutName, image(68 * 1024));
  assert.deepEqual(erases(withName).map((c) => c[3]), erases(withoutName).map((c) => c[3]));
  assert.equal(erases(withName).length, 5);
});

test('flash sets the address pointer once, then writes blocks from 2', async () => {
  const dev = makeFakeUsb();
  await flash(dev, image(5000));        // three 2048-byte blocks
  const setAddr = dev.calls.filter((c) => c[0] === 'out' && c[3][0] === 0x21);
  assert.equal(setAddr.length, 1);
  assert.deepEqual(setAddr[0][3], [0x21, 0x00, 0x00, 0x00, 0x08]);
  const blocks = dev.calls.filter((c) => c[0] === 'out' && c[2] >= 2);
  assert.deepEqual(blocks.map((c) => c[2]), [2, 3, 4]);
  assert.equal(blocks.at(-1)[3].length, 5000 - 2 * 2048);
});

test('flash reports erase then download progress', async () => {
  const dev = makeFakeUsb();
  const seen = [];
  await flash(dev, image(5000), { onProgress: (ev) => seen.push(ev) });
  assert.deepEqual([...new Set(seen.map((e) => e.op))], ['erase', 'download']);
  assert.equal(seen.at(-1).pct, 100);
  assert.ok(seen.every((e) => e.pct >= 0 && e.pct <= 100));
});

test('flash waits while the device reports itself busy', async () => {
  const statuses = [
    { status: 0, poll: 0, state: STATE_DFU_IDLE },   // the initial error check
    { status: 0, poll: 1, state: STATE_DNBUSY },
    { status: 0, poll: 1, state: STATE_DNBUSY },
    { status: 0, poll: 0, state: STATE_DNLOAD_IDLE },
  ];
  const dev = makeFakeUsb({ statuses });
  await flash(dev, image(2048));
  assert.equal(statuses.length, 0, 'every scripted status should have been consumed');
});

test('flash clears the dfuERROR state a fresh bootloader reports', async () => {
  // Measured, not hypothetical: a board that has just entered DFU via the wire
  // op answers GETSTATUS with errFIRMWARE/dfuERROR on the first read.
  const dev = makeFakeUsb({ statuses: [{ status: 0x0a, poll: 0, state: STATE_ERROR }] });
  await flash(dev, image(2048));
  assert.ok(dev.calls.some((c) => c[0] === 'out' && c[1] === CLRSTATUS));
  assert.ok(erases(dev).length > 0, 'the flash must continue after clearing');
});

test('flash raises a DfuError when the device reports a failed status', async () => {
  const dev = makeFakeUsb({ statuses: [
    { status: 0, poll: 0, state: STATE_DFU_IDLE },
    { status: 0x0a, poll: 0, state: STATE_ERROR },
  ] });
  await assert.rejects(() => flash(dev, image(2048)), DfuError);
});

test('flash raises a DfuError when a transfer stalls', async () => {
  const dev = makeFakeUsb();
  dev.controlTransferOut = async () => ({ status: 'stall' });
  await assert.rejects(() => flash(dev, image(2048)), DfuError);
});

test('flash treats the device detaching on leave as success', async () => {
  const dev = makeFakeUsb({ failLeave: true });
  await assert.doesNotReject(() => flash(dev, image(2048)));
  assert.equal(dev.closed, true);
});

test('flash closes the device even when the write fails', async () => {
  const dev = makeFakeUsb();
  dev.controlTransferOut = async () => ({ status: 'stall' });
  await assert.rejects(() => flash(dev, image(2048)));
  assert.equal(dev.closed, true);
});
