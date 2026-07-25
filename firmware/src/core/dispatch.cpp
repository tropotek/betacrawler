#include "core/dispatch.h"
#include <ArduinoJson.h>
#include <string.h>

namespace core {

static const char* errName(SetResult r) {
  switch (r) {
    case SetResult::Range:     return "range";
    case SetResult::BadEnum:   return "enum";
    case SetResult::TooLong:   return "toolong";
    case SetResult::WrongType: return "badtype";
    case SetResult::NoKey:     return "nokey";
    default:                   return "err";
  }
}

static void putValue(JsonObject o, const char* name, ParamId id, const Params& p) {
  if (defs()[id].type == ParamType::U8) o[name] = p.num(id);
  else                                  o[name] = p.str(id);
}

size_t writeTelemetry(char* out, size_t cap, const Telemetry& t) {
  JsonDocument doc;
  JsonObject o = doc["tlm"].to<JsonObject>();
  o["up"]   = t.up;
  o["clk"]  = t.clk;
  o["temp"] = t.temp;
  o["vdd"]  = t.vdd;
  o["ram"]  = t.ram;
  o["btn"]  = t.btn;
  return serializeJson(doc, out, cap);
}

size_t Dispatcher::handle(const Request& q, char* out, size_t cap) {
  JsonDocument doc;
  doc["id"] = q.id;

  if (!q.ok) {
    doc["ok"] = false;
    doc["err"] = q.err ? q.err : "err";
    return serializeJson(doc, out, cap);
  }

  switch (q.op) {
    case Op::Hello:
      doc["ok"] = true;
      doc["fw"] = "app-demo 0.1.0";
      doc["proto"] = kProtoVersion;
      doc["board"] = "blackpill_f411ce";
      break;

    case Op::Schema: {
      doc["ok"] = true;
      JsonArray arr = doc["params"].to<JsonArray>();
      for (uint8_t i = 0; i < PARAM_COUNT; ++i) {
        const ParamDef& d = defs()[i];
        JsonObject e = arr.add<JsonObject>();
        e["key"] = d.key;
        switch (d.type) {
          case ParamType::U8:
            e["type"] = "u8";
            e["min"] = d.minVal;
            e["max"] = d.maxVal;
            e["def"] = d.defNum;
            break;
          case ParamType::Enum: {
            e["type"] = "enum";
            JsonArray opts = e["options"].to<JsonArray>();
            for (uint8_t k = 0; k < d.optionCount; ++k) opts.add(d.options[k]);
            e["def"] = d.options[d.defNum];
            break;
          }
          case ParamType::Str:
            e["type"] = "str";
            e["maxlen"] = (uint32_t)d.maxLen;
            e["def"] = d.defStr;
            break;
        }
        e["label"] = d.label;
        if (d.unit) e["unit"] = d.unit;
      }
      break;
    }

    case Op::Get: {
      ParamId id;
      if (!findParam(q.key, &id)) { doc["ok"] = false; doc["err"] = "nokey"; break; }
      doc["ok"] = true;
      doc["key"] = q.key;
      putValue(doc.as<JsonObject>(), "val", id, p_);
      break;
    }

    case Op::GetAll: {
      doc["ok"] = true;
      JsonObject vals = doc["vals"].to<JsonObject>();
      for (uint8_t i = 0; i < PARAM_COUNT; ++i)
        putValue(vals, defs()[i].key, static_cast<ParamId>(i), p_);
      break;
    }

    case Op::Set: {
      ParamId id;
      if (!findParam(q.key, &id)) { doc["ok"] = false; doc["err"] = "nokey"; break; }
      SetResult r = q.hasStr ? p_.setStr(id, q.str)
                             : p_.setNum(id, q.num);
      if (r != SetResult::Ok) { doc["ok"] = false; doc["err"] = errName(r); break; }
      sink_.onParamChanged(id, p_);   // only ever on success
      doc["ok"] = true;
      break;
    }

    case Op::Save:
      doc["ok"] = store_.save(p_);
      if (!doc["ok"]) doc["err"] = "flash";
      break;

    case Op::Defaults:
      p_.loadDefaults();
      for (uint8_t i = 0; i < PARAM_COUNT; ++i)
        sink_.onParamChanged(static_cast<ParamId>(i), p_);
      doc["ok"] = true;
      break;

    case Op::Tlm:
      tlmOn_ = q.tlmOn;
      doc["ok"] = true;
      break;

    default:
      doc["ok"] = false;
      doc["err"] = "badop";
      break;
  }

  return serializeJson(doc, out, cap);
}

}  // namespace core
