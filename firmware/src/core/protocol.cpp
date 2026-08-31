#include "core/protocol.h"
#include <ArduinoJson.h>
#include <string.h>

namespace core {

void LineReader::reset() {
  len_ = 0;
  buf_[0] = '\0';
  dropping_ = false;
  lineOverflowed_ = false;
}

bool LineReader::feed(char c) {
  if (c == '\r') return false;
  if (c == '\n') {
    buf_[len_] = '\0';
    lineOverflowed_ = dropping_;
    len_ = 0;
    dropping_ = false;      // always recover for the next line
    return true;
  }
  if (len_ >= kMaxLineIn) {
    dropping_ = true;       // keep consuming until newline, discard content
    return false;
  }
  buf_[len_++] = c;
  return false;
}

static Op opFromString(const char* s) {
  if (strcmp(s, "hello") == 0)    return Op::Hello;
  if (strcmp(s, "schema") == 0)   return Op::Schema;
  if (strcmp(s, "get") == 0)      return Op::Get;
  if (strcmp(s, "getall") == 0)   return Op::GetAll;
  if (strcmp(s, "set") == 0)      return Op::Set;
  if (strcmp(s, "save") == 0)     return Op::Save;
  if (strcmp(s, "defaults") == 0) return Op::Defaults;
  if (strcmp(s, "revert") == 0)   return Op::Revert;
  if (strcmp(s, "tlm") == 0)      return Op::Tlm;
  if (strcmp(s, "dfu") == 0)      return Op::Dfu;
  return Op::Unknown;
}

Request parseRequest(const char* line) {
  Request q;
  JsonDocument doc;
  if (deserializeJson(doc, line) != DeserializationError::Ok) {
    q.err = "badjson";
    return q;
  }
  q.id = doc["id"] | 0u;

  const char* opStr = doc["op"] | "";
  q.op = opFromString(opStr);
  if (q.op == Op::Unknown) {
    q.err = "badop";     // id already captured, so a reply is still possible
    return q;
  }

  const char* key = doc["key"] | "";
  strncpy(q.key, key, sizeof(q.key) - 1);

  JsonVariant val = doc["val"];
  if (!val.isNull()) {
    if (val.is<const char*>()) {
      const char* s = val.as<const char*>();
      if (strlen(s) > kMaxStrLen) {
        q.err = "toolong";
        return q;   // q.ok stays false — same pattern as the badjson/badop early returns
      }
      q.hasStr = true;
      strncpy(q.str, s, kMaxStrLen);
      q.str[kMaxStrLen] = '\0';
    } else {
      q.hasNum = true;
      q.num = val.as<int32_t>();
    }
  }

  q.tlmOn = doc["on"] | false;
  q.ok = true;
  return q;
}

}  // namespace core
