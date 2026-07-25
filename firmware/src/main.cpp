#include <Arduino.h>
#include "core/dispatch.h"
#include "core/protocol.h"
#include "hardware.h"
#include "storage.h"

using namespace core;

static Params      g_params;
static hw::LedDriver g_led;
static LineReader  g_reader;

struct ArduinoSink : HardwareSink {
  void onParamChanged(ParamId id, const Params& p) override {
    if (id == PARAM_LED_MODE || id == PARAM_LED_BLINK_HZ) {
      g_led.apply(p.num(PARAM_LED_MODE), p.num(PARAM_LED_BLINK_HZ));
    }
  }
};

static ArduinoSink g_sink;
static FlashStore  g_store;
static Dispatcher  g_dispatch(g_params, g_sink, g_store);

static char g_out[kMaxLineOut];
static uint32_t g_lastTlm = 0;

void setup() {
  Serial.begin(115200);
  hw::begin();
  g_led.begin();
  g_store.load(&g_params);   // falls back to defaults on magic/version/CRC mismatch
  g_led.apply(g_params.num(PARAM_LED_MODE), g_params.num(PARAM_LED_BLINK_HZ));
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

  g_led.tick(now);

  if (g_dispatch.telemetryEnabled()) {
    uint32_t period = 1000u / (uint32_t)g_params.num(PARAM_TLM_RATE);
    if (period == 0) period = 1;
    if (now - g_lastTlm >= period) {
      g_lastTlm = now;
      Telemetry t;
      hw::readTelemetry(&t);
      size_t n = writeTelemetry(g_out, sizeof(g_out), t);
      if (n > 0) Serial.println(g_out);
    }
  }
}
