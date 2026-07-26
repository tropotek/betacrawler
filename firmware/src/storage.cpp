#include <Arduino.h>
#include <EEPROM.h>
#include <string.h>
#include "storage.h"

using namespace core;

static const uint32_t kMagic = 0x4D444C31;  // "MDL1"
// 2: header gained `fingerprint` when parameters moved into modules.
static const uint16_t kVersion = 2;

struct Header {
  uint32_t magic;
  uint16_t version;
  uint16_t crc;
  // Identifies the parameter layout this blob was written against (see
  // Registry::fingerprint). The magic/version/CRC trio proves the bytes are
  // intact; this proves they still *mean* the same thing. Without it,
  // flipping FEATURE_LED off would leave a valid-looking blob whose values
  // silently shift one parameter to the left on the next load.
  uint32_t fingerprint;
};

static uint16_t crc16(const uint8_t* d, size_t n) {
  uint16_t crc = 0xFFFF;
  for (size_t i = 0; i < n; ++i) {
    crc ^= (uint16_t)d[i] << 8;
    for (uint8_t b = 0; b < 8; ++b)
      crc = (crc & 0x8000) ? (uint16_t)((crc << 1) ^ 0x1021) : (uint16_t)(crc << 1);
  }
  return crc;
}

bool FlashStore::save(const core::Params& p) {
  const uint8_t* payload = reinterpret_cast<const uint8_t*>(p.raw());
  const size_t payloadLen = sizeof(Value) * reg_.paramCount();

  Header h;
  h.magic = kMagic;
  h.version = kVersion;
  h.crc = crc16(payload, payloadLen);
  h.fingerprint = reg_.fingerprint();

  eeprom_buffer_fill();                       // pull page into RAM
  size_t off = 0;
  const uint8_t* hp = reinterpret_cast<const uint8_t*>(&h);
  for (size_t i = 0; i < sizeof(Header); ++i) eeprom_buffered_write_byte(off++, hp[i]);
  for (size_t i = 0; i < payloadLen; ++i)     eeprom_buffered_write_byte(off++, payload[i]);
  eeprom_buffer_flush();                      // ONE erase+write, stalls ~1s

  // eeprom_buffer_flush() is void -- it cannot report a write failure. Read
  // the header back through the same buffered-read path load() uses and
  // confirm it matches what was just written, so a genuine flash failure
  // has a real (if rare) way to surface as {"ok":false,"err":"flash"}
  // instead of that being structurally dead code.
  eeprom_buffer_fill();
  Header check;
  uint8_t* cp = reinterpret_cast<uint8_t*>(&check);
  size_t roff = 0;
  for (size_t i = 0; i < sizeof(Header); ++i) cp[i] = eeprom_buffered_read_byte(roff++);

  return check.magic == h.magic && check.version == h.version &&
         check.crc == h.crc && check.fingerprint == h.fingerprint;
}

bool FlashStore::load(core::Params* p) {
  eeprom_buffer_fill();

  Header h;
  uint8_t* hp = reinterpret_cast<uint8_t*>(&h);
  size_t off = 0;
  for (size_t i = 0; i < sizeof(Header); ++i) hp[i] = eeprom_buffered_read_byte(off++);

  // Any mismatch -- corrupt, older firmware, or a different set of enabled
  // modules -- falls back to defaults rather than guessing.
  if (h.magic != kMagic || h.version != kVersion) return false;
  if (h.fingerprint != reg_.fingerprint()) return false;

  const size_t payloadLen = sizeof(Value) * reg_.paramCount();
  static uint8_t buf[sizeof(Value) * FW_MAX_PARAMS];
  for (size_t i = 0; i < payloadLen; ++i) buf[i] = eeprom_buffered_read_byte(off++);

  if (crc16(buf, payloadLen) != h.crc) return false;

  memcpy(p->rawMutable(), buf, payloadLen);
  return true;
}
