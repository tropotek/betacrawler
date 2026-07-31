#include <Arduino.h>
#include "config.h"
#include "core/dispatch.h"
#include "core/protocol.h"
#include "core/registry.h"
#include "core/boot_log.h"
#include "core/version.h"
#include "dfu.h"
#include "storage.h"

using namespace core;

// No feature is named anywhere in this file. Which modules exist is decided by
// the board header via src/modules.cpp; everything here works off the registry.
// DfuTrigger is not a module -- it has no parameters and no telemetry -- so it
// is wired directly, exactly like FlashStore, and compiles to a stub that
// answers "unsupported" when FEATURE_DFU is off.
static Registry   g_reg;
static Params     g_params(g_reg);
static FlashStore g_store(g_reg);
static dfu::DfuTrigger g_boot;
static Dispatcher g_dispatch(g_reg, g_params, g_store, &g_boot);
static LineReader g_reader;

static char     g_out[kMaxLineOut];
static TlmValue g_tlm[FW_MAX_TLM];
static uint32_t g_lastTlm = 0;
static ParamId  g_tlmRateId = kNoParam;

// Space between the heap top and the current stack -- the cheapest "is this
// build about to run out of RAM" signal there is, and worth capturing at boot
// before anything has had a chance to grow.
extern "C" char* sbrk(int incr);
static int freeRamBytes() {
  char top;
  return (int)(&top - (char*)sbrk(0));
}

// Replays everything recorded during setup(). Called at the end of boot (for
// whoever is watching the serial monitor) and again after every `hello` --
// which is what makes boot health visible in the app's Terminal, since the
// host connects long after these lines were produced.
static void emitBootLog() {
  BootLog& b = bootLog();
  for (uint8_t i = 0; i < b.count(); ++i) {
    size_t n = writeLog(g_out, sizeof(g_out), b.line(i));
    if (n > 0) Serial.println(g_out);
  }
  if (b.dropped()) {
    size_t n = writeLog(g_out, sizeof(g_out), "boot: log full, lines dropped");
    if (n > 0) Serial.println(g_out);
  }
}

void setup() {
  Serial.begin(FW_SERIAL_BAUD);
  registerModules(g_reg);
  g_dispatch.setWifiScanner(wifiScanner());

  // g_params was constructed during static init, before registerModules()
  // ran, so it defaulted an empty table. Now that the registry is populated,
  // load the real defaults before anything reads a value.
  g_params.loadDefaults();

  g_reg.findParam("tlm.rate", &g_tlmRateId);

  char msg[BootLog::kMaxLen];
  snprintf(msg, sizeof(msg), "boot: %s %s (%s) built %s", projectName(),
           version(), boardId(), buildDate());
  bootLog().add(msg);

  // Whether saved settings survived is a genuine health fact, not a detail: a
  // fingerprint mismatch quietly reverting a configured board to defaults is
  // exactly the sort of thing that is baffling without a boot record.
  bool stored = g_store.load(&g_params);   // falls back to defaults on magic/version/fingerprint/CRC mismatch
  snprintf(msg, sizeof(msg), "boot: settings=%s", stored ? "restored" : "defaults");
  bootLog().add(msg);

  g_reg.begin(g_params);   // modules may add their own boot lines here

  snprintf(msg, sizeof(msg), "boot: modules=%u params=%u tlm=%u ram=%dB",
           (unsigned)g_reg.moduleCount(), (unsigned)g_reg.paramCount(),
           (unsigned)g_reg.tlmCount(), freeRamBytes());
  bootLog().add(msg);

  // Push every stored value at its module, exactly as the `defaults` op does.
  // This replaces main.cpp's old explicit g_led.apply(...) call: no module is
  // special-cased, so a new one is picked up here for free.
  for (uint8_t i = 0; i < g_reg.paramCount(); ++i) g_reg.notify(i, g_params);

  emitBootLog();
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
        // Follow a handshake with the boot record. Deliberately after the
        // response and as separate unsolicited lines, so `hello`'s shape is
        // unchanged and every existing consumer keeps working.
        if (q.ok && q.op == Op::Hello) emitBootLog();
        // A `dfu` request only ARMED the reboot; it happens here, once the
        // response is genuinely on the wire. Reset any earlier and the host
        // never sees the ack, leaving it unable to distinguish "rebooting
        // into the bootloader" from "the board just died".
        if (g_boot.pending()) {
          Serial.flush();
          delay(50);        // USB CDC: flush() queues, the host still has to poll
          g_boot.reboot();  // does not return
        }
      }
    }
  }

  g_reg.tick(now);

  // Generic unsolicited-push channel (core::Module::pollPush) -- currently
  // used by exactly one module, but nothing here names it. A second module
  // that needs one composes for free.
  {
    size_t n = g_reg.pollPush(g_out, sizeof(g_out));
    if (n > 0) Serial.println(g_out);
  }

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
