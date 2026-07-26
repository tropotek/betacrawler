#include "core/dispatch.h"
#include "core/version.h"
#include <ArduinoJson.h>
#include <stdio.h>
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

static void putValue(JsonObject o, const char* name, ParamId id,
                     const Registry& reg, const Params& p) {
  if (reg.paramDef(id).type == ParamType::U8) o[name] = p.num(id);
  else                                        o[name] = p.str(id);
}

size_t writeTelemetry(char* out, size_t cap, const Registry& reg, const TlmValue* vals) {
  JsonDocument doc;
  JsonObject o = doc["tlm"].to<JsonObject>();
  for (uint8_t i = 0; i < reg.tlmCount(); ++i) {
    const TlmDef& d = reg.tlmDef(i);
    switch (d.type) {
      case TlmType::U32: o[d.key] = vals[i].u; break;
      case TlmType::I32: o[d.key] = vals[i].i; break;
      case TlmType::F32: o[d.key] = vals[i].f; break;
    }
  }
  return serializeJson(doc, out, cap);
}

size_t writeLog(char* out, size_t cap, const char* msg) {
  if (!out || cap == 0) return 0;
  JsonDocument doc;
  doc["log"] = msg;
  // measureJson excludes the NUL that serializeJson writes. A line that will
  // not fit is dropped rather than truncated: half a JSON object on the wire
  // would desync the host's line parser, which is worse than a lost message.
  if (measureJson(doc) + 1 > cap) { out[0] = '\0'; return 0; }
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
    case Op::Hello: {
      doc["ok"] = true;
      // `fw` stays a single display string so every existing consumer
      // (app.js's navbar, docs/api.md) keeps working untouched; the
      // structured fields beside it are purely additive.
      char fw[64];
      snprintf(fw, sizeof(fw), "%s %s", projectName(), version());
      doc["fw"]    = fw;
      doc["name"]  = projectName();
      doc["ver"]   = version();
      doc["built"] = buildDate();
      doc["proto"] = kProtoVersion;
      doc["board"] = boardId();
      // Which modules this build actually has. Lets the app tell "this board
      // has no LED" apart from "the LED controls failed to load".
      JsonArray mods = doc["mods"].to<JsonArray>();
      for (uint8_t i = 0; i < reg_.moduleCount(); ++i) mods.add(reg_.moduleId(i));
      // Optional device capabilities that are not modules -- things the
      // device can DO rather than things it HAS. Additive, exactly as `mods`
      // was: firmware without it simply omits the key, and the app treats a
      // missing `caps` as "none", so no existing consumer is affected.
      JsonArray caps = doc["caps"].to<JsonArray>();
      if (boot_ && boot_->supported()) caps.add("dfu");
      break;
    }

    case Op::Schema: {
      doc["ok"] = true;
      JsonArray arr = doc["params"].to<JsonArray>();
      for (uint8_t i = 0; i < reg_.paramCount(); ++i) {
        const ParamDef& d = reg_.paramDef(i);
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
        e["group"] = reg_.paramGroup(i);
      }

      // Telemetry descriptor: the same "firmware is the source of truth" rule
      // the parameter table already follows, extended to the Telemetry page.
      // The browser renders cards straight from this, so a new sensor module
      // needs no JavaScript change to appear.
      JsonArray tarr = doc["tlm"].to<JsonArray>();
      for (uint8_t i = 0; i < reg_.tlmCount(); ++i) {
        const TlmDef& d = reg_.tlmDef(i);
        JsonObject e = tarr.add<JsonObject>();
        e["key"] = d.key;
        e["label"] = d.label;
        if (d.unit) e["unit"] = d.unit;
        if (d.div > 1) e["div"] = d.div;
        if (d.dec) e["dec"] = d.dec;
        e["group"] = reg_.tlmGroup(i);
      }
      break;
    }

    case Op::Get: {
      ParamId id;
      if (!reg_.findParam(q.key, &id)) { doc["ok"] = false; doc["err"] = "nokey"; break; }
      doc["ok"] = true;
      doc["key"] = q.key;
      putValue(doc.as<JsonObject>(), "val", id, reg_, p_);
      break;
    }

    case Op::GetAll: {
      doc["ok"] = true;
      JsonObject vals = doc["vals"].to<JsonObject>();
      for (uint8_t i = 0; i < reg_.paramCount(); ++i)
        putValue(vals, reg_.paramDef(i).key, i, reg_, p_);
      break;
    }

    case Op::Set: {
      ParamId id;
      if (!reg_.findParam(q.key, &id)) { doc["ok"] = false; doc["err"] = "nokey"; break; }
      SetResult r = q.hasStr ? p_.setStr(id, q.str)
                             : p_.setNum(id, q.num);
      if (r != SetResult::Ok) { doc["ok"] = false; doc["err"] = errName(r); break; }
      reg_.notify(id, p_);   // only ever on success
      doc["ok"] = true;
      break;
    }

    case Op::Save:
      doc["ok"] = store_.save(p_);
      if (!doc["ok"]) doc["err"] = "flash";
      break;

    case Op::Defaults:
      p_.loadDefaults();
      for (uint8_t i = 0; i < reg_.paramCount(); ++i) reg_.notify(i, p_);
      doc["ok"] = true;
      break;

    case Op::Tlm:
      tlmOn_ = q.tlmOn;
      doc["ok"] = true;
      break;

    case Op::Dfu:
      // Same shape as Op::Save's `err:"flash"` -- the seam returns a bool and
      // the failure gets a name. `enterDfu()` only ARMS the reboot; main.cpp
      // performs it after this response has been flushed to the host.
      doc["ok"] = boot_ && boot_->enterDfu();
      if (!doc["ok"]) doc["err"] = "nodfu";
      break;

    case Op::Unknown:
      // parseRequest() never lets an Unknown op reach here with q.ok true
      // (it sets q.err="badop" and returns early instead) -- this case only
      // exists so the switch stays exhaustive over every Op value. With no
      // `default:`, -Wswitch will flag it (and any future Op added without
      // a case) at compile time instead of silently falling through here.
      doc["ok"] = false;
      doc["err"] = "badop";
      break;
  }

  return serializeJson(doc, out, cap);
}

}  // namespace core
