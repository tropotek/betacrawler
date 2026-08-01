#include "storage.h"
#include "config.h"

#if FW_MCU_ESP32

#include <Arduino.h>
#include <EEPROM.h>
#include <string.h>

using namespace core;

static const uint32_t kMagic = 0x4D444C31;  // "MDL1"
static const uint16_t kVersion = 2;

struct Header {
  uint32_t magic;
  uint16_t version;
  uint16_t crc;
  uint32_t fingerprint;
};

// arduino-esp32's EEPROM library emulates this many bytes of byte-addressable
// storage inside an "eeprom" NVS namespace it opens in the standard "nvs"
// partition every default partition table (esp32dev's included) already
// reserves -- there is no separate "eeprom" partition. Sized exactly for
// what this board's parameter table needs (a Header plus FW_MAX_PARAMS
// Values), not with headroom.
static const size_t kEepromSize = sizeof(Header) + sizeof(Value) * FW_MAX_PARAMS;

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

  EEPROM.begin(kEepromSize);
  size_t off = 0;
  const uint8_t* hp = reinterpret_cast<const uint8_t*>(&h);
  for (size_t i = 0; i < sizeof(Header); ++i) EEPROM.write(off++, hp[i]);
  for (size_t i = 0; i < payloadLen; ++i)     EEPROM.write(off++, payload[i]);

  // Unlike the STM32 buffered-EEPROM API, commit() reports failure directly
  // -- but a real integrity check requires reloading from flash, not the
  // in-RAM buffer. Call EEPROM.end() then EEPROM.begin() again to force a
  // fresh reload from the actual "eeprom" NVS namespace, making the readback
  // below a genuine flash round-trip, exactly like the STM32 side's
  // eeprom_buffer_fill() call.
  if (!EEPROM.commit()) return false;

  EEPROM.end();
  EEPROM.begin(kEepromSize);

  Header check;
  uint8_t* cp = reinterpret_cast<uint8_t*>(&check);
  size_t roff = 0;
  for (size_t i = 0; i < sizeof(Header); ++i) cp[i] = EEPROM.read(roff++);

  return check.magic == h.magic && check.version == h.version &&
         check.crc == h.crc && check.fingerprint == h.fingerprint;
}

bool FlashStore::load(core::Params* p) {
  EEPROM.begin(kEepromSize);

  Header h;
  uint8_t* hp = reinterpret_cast<uint8_t*>(&h);
  size_t off = 0;
  for (size_t i = 0; i < sizeof(Header); ++i) hp[i] = EEPROM.read(off++);

  // Any mismatch -- corrupt, older firmware, or a different set of enabled
  // modules -- falls back to defaults rather than guessing.
  if (h.magic != kMagic || h.version != kVersion) return false;
  if (h.fingerprint != reg_.fingerprint()) return false;

  const size_t payloadLen = sizeof(Value) * reg_.paramCount();
  static uint8_t buf[sizeof(Value) * FW_MAX_PARAMS];
  for (size_t i = 0; i < payloadLen; ++i) buf[i] = EEPROM.read(off++);

  if (crc16(buf, payloadLen) != h.crc) return false;

  memcpy(p->rawMutable(), buf, payloadLen);
  return true;
}

#endif  // FW_MCU_ESP32
