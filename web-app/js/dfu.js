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
