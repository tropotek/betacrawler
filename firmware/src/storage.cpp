#include <Arduino.h>
#include <EEPROM.h>
#include <string.h>
#include "storage.h"

using namespace core;

static const uint32_t kMagic = 0x4D444C31;  // "MDL1"
static const uint16_t kVersion = 1;

struct Header {
  uint32_t magic;
  uint16_t version;
  uint16_t crc;
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
  const size_t payloadLen = sizeof(Value) * PARAM_COUNT;

  Header h;
  h.magic = kMagic;
  h.version = kVersion;
  h.crc = crc16(payload, payloadLen);

  eeprom_buffer_fill();                       // pull page into RAM
  size_t off = 0;
  const uint8_t* hp = reinterpret_cast<const uint8_t*>(&h);
  for (size_t i = 0; i < sizeof(Header); ++i) eeprom_buffered_write_byte(off++, hp[i]);
  for (size_t i = 0; i < payloadLen; ++i)     eeprom_buffered_write_byte(off++, payload[i]);
  eeprom_buffer_flush();                      // ONE erase+write, stalls ~1s
  return true;
}

bool FlashStore::load(core::Params* p) {
  eeprom_buffer_fill();

  Header h;
  uint8_t* hp = reinterpret_cast<uint8_t*>(&h);
  size_t off = 0;
  for (size_t i = 0; i < sizeof(Header); ++i) hp[i] = eeprom_buffered_read_byte(off++);

  if (h.magic != kMagic || h.version != kVersion) return false;

  const size_t payloadLen = sizeof(Value) * PARAM_COUNT;
  static uint8_t buf[sizeof(Value) * PARAM_COUNT];
  for (size_t i = 0; i < payloadLen; ++i) buf[i] = eeprom_buffered_read_byte(off++);

  if (crc16(buf, payloadLen) != h.crc) return false;

  memcpy(p->rawMutable(), buf, payloadLen);
  return true;
}
