// DfuSe (the STM32 ROM bootloader's DFU dialect) over WebUSB. Takes an injected
// USBDevice-shaped object -- no navigator.usb reference here -- so it tests
// against a fake, the way webserial-link.js takes an injected SerialPort.

export class DfuError extends Error {}

export const FLASH_ORIGIN = 0x08000000;

// Bounds for the sanity check on an image the user picked by hand. SRAM_HI is
// INCLUSIVE: a real betacrawler build puts its initial stack pointer at exactly
// 0x20020000, the top of the F411's 128KB SRAM.
const SRAM_LO = 0x20000000, SRAM_HI = 0x20020000;
const FLASH_LO = 0x08000000, FLASH_HI = 0x08080000;
const MIN_IMAGE = 1024, MAX_IMAGE = 512 * 1024;

const hex8 = (n) => `0x${n.toString(16).padStart(8, '0')}`;

/** Reject anything that is obviously not a raw STM32F4 image. Applied to files
 *  the user picks; a bundled image is covered by its sha256 instead. */
export function validateImage(bytes) {
  if (bytes.length < MIN_IMAGE) {
    throw new DfuError(`image is only ${bytes.length} bytes -- too small to be firmware`);
  }
  if (bytes.length > MAX_IMAGE) {
    throw new DfuError(`image is ${bytes.length} bytes, larger than the 512KB flash`);
  }
  const view = new DataView(bytes.buffer, bytes.byteOffset, bytes.byteLength);
  const msp = view.getUint32(0, true);
  const reset = view.getUint32(4, true);
  if (msp < SRAM_LO || msp > SRAM_HI) {
    throw new DfuError(
      `not a raw firmware binary: initial stack pointer ${hex8(msp)} is outside SRAM. `
      + 'If this is firmware.elf or firmware.hex, use firmware.bin instead.');
  }
  if (reset < FLASH_LO || reset >= FLASH_HI || !(reset & 1)) {
    throw new DfuError(
      `not a raw firmware binary: reset vector ${hex8(reset)} is not a Thumb address in flash`);
  }
}

function buildLayout(origin, groups) {
  const out = [];
  let addr = origin;
  for (const { count, size } of groups) {
    for (let i = 0; i < count; i += 1) {
      out.push({ start: addr, size });
      addr += size;
    }
  }
  return out;
}

// The STM32F4 internal-flash map. Chrome surfaces this bootloader's
// interfaceName as null, so this is the map that actually runs; the parser
// below is what would adapt to a different geometry if it ever appears.
export const F4_DEFAULT_LAYOUT = buildLayout(FLASH_ORIGIN, [
  { count: 4, size: 16 * 1024 },
  { count: 1, size: 64 * 1024 },
  { count: 3, size: 128 * 1024 },
]);

// One group of `@Internal Flash  /0x08000000/04*016Kg,01*064Kg,03*128Kg`.
const SECTOR_GROUP = /^(\d+)\*(\d+)([KMB]?)\s*[a-gA-G]$/;
const UNITS = { '': 1, K: 1024, M: 1024 * 1024, B: 1 };

/** Sector map from the alt setting's name, or the F4 default when the device
 *  does not report one. Sectors are non-uniform, so erase addresses cannot be
 *  produced by stepping a constant. */
export function parseMemoryLayout(name) {
  if (typeof name !== 'string') return F4_DEFAULT_LAYOUT;
  const parts = name.split('/');
  if (parts.length < 3) return F4_DEFAULT_LAYOUT;
  const origin = Number.parseInt(parts[1], 16);
  if (origin !== FLASH_ORIGIN) return F4_DEFAULT_LAYOUT;
  const groups = [];
  for (const spec of parts[2].split(',')) {
    const m = SECTOR_GROUP.exec(spec.trim());
    if (!m) return F4_DEFAULT_LAYOUT;
    groups.push({ count: Number(m[1]), size: Number(m[2]) * UNITS[m[3]] });
  }
  return groups.length ? buildLayout(origin, groups) : F4_DEFAULT_LAYOUT;
}

/** The sectors an image of `length` bytes written at `origin` overlaps. */
export function sectorsFor(layout, origin, length) {
  const end = origin + length;
  return layout.filter((s) => s.start < end && s.start + s.size > origin);
}

// --- the flash sequence -------------------------------------------------------

// DFU class requests, and the DfuSe commands carried in block 0.
const DFU_DNLOAD = 1, DFU_GETSTATUS = 3, DFU_CLRSTATUS = 4;
const DFUSE_SET_ADDRESS = 0x21, DFUSE_ERASE = 0x41;
const STATE_DNLOAD_IDLE = 5, STATE_DFU_ERROR = 10;
const INTERFACE = 0;
// The STM32 ROM bootloader's wTransferSize. Fixed rather than read from the DFU
// functional descriptor: this module targets exactly one bootloader.
const TRANSFER_SIZE = 2048;

const sleep = (ms) => new Promise((resolve) => setTimeout(resolve, ms));
const setup = (request, value) => ({
  requestType: 'class', recipient: 'interface', request, value, index: INTERFACE,
});

function le32(value) {
  const bytes = new Uint8Array(4);
  new DataView(bytes.buffer).setUint32(0, value, true);
  return bytes;
}

async function getStatus(dev) {
  const r = await dev.controlTransferIn(setup(DFU_GETSTATUS, 0), 6);
  if (r.status !== 'ok') throw new DfuError('the device did not answer DFU_GETSTATUS');
  return {
    status: r.data.getUint8(0),
    pollTimeout: r.data.getUint8(1) | (r.data.getUint8(2) << 8) | (r.data.getUint8(3) << 16),
    state: r.data.getUint8(4),
  };
}

// Honours the device's own bwPollTimeout rather than a fixed sleep -- erasing a
// 128KB sector asks for far longer than a 2KB write does.
async function pollUntilIdle(dev) {
  let st = await getStatus(dev);
  while (st.state !== STATE_DNLOAD_IDLE && st.state !== STATE_DFU_ERROR) {
    if (st.pollTimeout) await sleep(st.pollTimeout);
    st = await getStatus(dev);
  }
  if (st.status !== 0 || st.state === STATE_DFU_ERROR) {
    throw new DfuError(`the device rejected the write (DFU status 0x${st.status.toString(16)})`);
  }
  return st;
}

async function download(dev, blockNum, data) {
  const r = await dev.controlTransferOut(setup(DFU_DNLOAD, blockNum), data);
  if (r.status !== 'ok') throw new DfuError(`DFU_DNLOAD block ${blockNum} failed`);
}

async function dfuseCommand(dev, command, addr) {
  await download(dev, 0, new Uint8Array([command, ...le32(addr)]));
  await pollUntilIdle(dev);
}

// A bootloader that has just been entered reports errFIRMWARE/dfuERROR on its
// first status read, so clearing is part of connecting, not error handling.
async function clearErrorState(dev) {
  const st = await getStatus(dev);
  if (st.state === STATE_DFU_ERROR) {
    await dev.controlTransferOut(setup(DFU_CLRSTATUS, 0));
    await getStatus(dev);
  }
}

function layoutOf(dev) {
  const alt = dev.configuration?.interfaces
    ?.find((i) => i.interfaceNumber === INTERFACE)
    ?.alternates?.find((a) => a.alternateSetting === 0);
  return parseMemoryLayout(alt?.interfaceName);
}

/** Write `bytes` to the board's internal flash and leave DFU mode.
 *  onProgress receives {op, pct, line}; op is 'erase' then 'download'. */
export async function flash(usbDevice, bytes, { onProgress = () => {} } = {}) {
  const data = bytes instanceof Uint8Array ? bytes : new Uint8Array(bytes);
  await usbDevice.open();
  if (!usbDevice.configuration) await usbDevice.selectConfiguration(1);
  await usbDevice.claimInterface(INTERFACE);
  await usbDevice.selectAlternateInterface(INTERFACE, 0);
  try {
    await clearErrorState(usbDevice);

    const sectors = sectorsFor(layoutOf(usbDevice), FLASH_ORIGIN, data.length);
    for (const [i, sector] of sectors.entries()) {
      await dfuseCommand(usbDevice, DFUSE_ERASE, sector.start);
      onProgress({
        op: 'erase',
        pct: Math.round(((i + 1) / sectors.length) * 100),
        line: `erased ${hex8(sector.start)}`,
      });
    }

    // Set once: the bootloader advances the pointer itself as the block number
    // climbs from 2, which is what dfu-util does too.
    await dfuseCommand(usbDevice, DFUSE_SET_ADDRESS, FLASH_ORIGIN);
    for (let offset = 0, block = 2; offset < data.length; offset += TRANSFER_SIZE, block += 1) {
      await download(usbDevice, block, data.subarray(offset, offset + TRANSFER_SIZE));
      await pollUntilIdle(usbDevice);
      const written = Math.min(offset + TRANSFER_SIZE, data.length);
      onProgress({
        op: 'download',
        pct: Math.round((written / data.length) * 100),
        line: `${written}/${data.length} bytes`,
      });
    }

    // Manifest and leave. The device resets into the new application here, so
    // this transfer and the status read after it may never be answered -- that
    // is the success case, not a failure.
    try {
      await download(usbDevice, 0, new Uint8Array(0));
      await getStatus(usbDevice);
    } catch { /* already detached */ }
  } finally {
    try { await usbDevice.close(); } catch { /* already gone */ }
  }
}
