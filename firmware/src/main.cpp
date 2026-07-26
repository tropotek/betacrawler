#include <Arduino.h>
#include "config.h"
#include "core/dispatch.h"
#include "core/protocol.h"
#include "core/registry.h"
#include "storage.h"

using namespace core;

// No feature is named anywhere in this file. Which modules exist is decided by
// the board header via src/modules.cpp; everything here works off the registry.
static Registry   g_reg;
static Params     g_params(g_reg);
static FlashStore g_store(g_reg);
static Dispatcher g_dispatch(g_reg, g_params, g_store);
static LineReader g_reader;

static char     g_out[kMaxLineOut];
static TlmValue g_tlm[FW_MAX_TLM];
static uint32_t g_lastTlm = 0;
static ParamId  g_tlmRateId = kNoParam;

void setup() {
  Serial.begin(FW_SERIAL_BAUD);
  registerModules(g_reg);

  // g_params was constructed during static init, before registerModules()
  // ran, so it defaulted an empty table. Now that the registry is populated,
  // load the real defaults before anything reads a value.
  g_params.loadDefaults();

  g_reg.findParam("tlm.rate", &g_tlmRateId);
  g_store.load(&g_params);   // falls back to defaults on magic/version/fingerprint/CRC mismatch
  g_reg.begin();

  // Push every stored value at its module, exactly as the `defaults` op does.
  // This replaces main.cpp's old explicit g_led.apply(...) call: no module is
  // special-cased, so a new one is picked up here for free.
  for (uint8_t i = 0; i < g_reg.paramCount(); ++i) g_reg.notify(i, g_params);
}

void loop() {
  uint32_t now = millis();

  while (Serial.available()) {
    if (g_reader.feed((char)Serial.read())) {
      if (g_reader.overflowed()) {
        Serial.println("{\"ok\":false,\"err\":\"overflow\"}");
      } else if (g_reader.line()[0] != '\0') {
        Request q = parseRequest(g_reader.line());
        size_t n = g_dispatch.handle(q, g_out, sizeof(g_out));
        if (n > 0) Serial.println(g_out);
      }
    }
  }

  g_reg.tick(now);

  if (g_dispatch.telemetryEnabled() && g_tlmRateId != kNoParam) {
    uint32_t period = 1000u / (uint32_t)g_params.num(g_tlmRateId);
    if (period == 0) period = 1;
    if (now - g_lastTlm >= period) {
      g_lastTlm = now;
      g_reg.collectTelemetry(g_tlm);
      size_t n = writeTelemetry(g_out, sizeof(g_out), g_reg, g_tlm);
      if (n > 0) Serial.println(g_out);
    }
  }
}
