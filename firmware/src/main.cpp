#include <Arduino.h>
#include "config.h"
#include "core/dispatch.h"
#include "core/protocol.h"
#include "core/registry.h"
#include "core/boot_log.h"
#include "core/loop_stats.h"
#include "core/version.h"
#include "dfu.h"
#include "status_led.h"
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
static status_led::StatusLed g_statusLed;
static Dispatcher g_dispatch(g_reg, g_params, g_store, &g_boot);
static LineReader g_reader;

static char     g_out[kMaxLineOut];
static TlmValue g_tlm[FW_MAX_TLM];
static uint32_t g_lastTlm = 0;
static ParamId  g_tlmRateId = kNoParam;

// Longest a single write() call is allowed to make zero progress before
// writeLine() gives up on the rest of a line -- see writeLine()'s own
// comment for why this exists at all. Real disconnects (unplugged, app
// closed) stay unwritable far longer than this; a still-connected host that
// was merely slow to drain one USB packet recovers within it.
constexpr uint32_t kWriteStallTimeoutMs = 200;

// USBSerial::write() (libraries/USBDevice/src/USBSerial.cpp, this board's
// Serial) can return fewer bytes than requested: its own retry loop bails
// the instant CDC_connected() reads false even once -- which usbd_cdc_if.c's
// CDC_connected() does whenever a single in-flight USB packet hasn't
// completed within USB_CDC_TRANSMIT_TIMEOUT (a handful of ms) of starting,
// a momentary host-side stall rather than a real disconnect -- and it never
// resumes on its own. For a short line this is harmless (the odds of a
// stall landing inside one write() call are low); for the schema response,
// by far the largest and slowest-to-transmit line this firmware sends,
// hitting that window even once used to silently truncate it -- observed on
// real hardware, not theoretical. Retrying the remainder here is what a
// blocking write is supposed to do; USBSerial::write() just doesn't.
static void writeLine(const char* buf, size_t len) {
  size_t sent = 0;
  uint32_t stallStart = 0;
  while (sent < len) {
    size_t n = Serial.write(reinterpret_cast<const uint8_t*>(buf) + sent, len - sent);
    sent += n;
    if (sent >= len) break;
    if (n == 0) {
      uint32_t now = millis();
      if (stallStart == 0) stallStart = now;
      if (now - stallStart > kWriteStallTimeoutMs) return;   // host genuinely gone
      delay(1);
    } else {
      stallStart = 0;
    }
  }
  Serial.write(reinterpret_cast<const uint8_t*>("\r\n"), 2);
}

// Space between the heap top and the current stack -- the cheapest "is this
// build about to run out of RAM" signal there is, and worth capturing at boot
// before anything has had a chance to grow.
//
// STM32's Arduino core declares sbrk() nowhere public, so this file has
// always had to declare it itself and call it directly. The ESP32 core's own
// <unistd.h> (dragged in transitively by Arduino.h -> HardwareSerial.h ->
// ... on that platform) DOES declare a compatible-looking `void*
// sbrk(ptrdiff_t)` -- but arduino-esp32 does not implement a classic
// sbrk()-growable heap at all (it uses a multi-region heap-caps allocator
// instead), so calling it is not just a declaration mismatch to paper over:
// it hits syscall_not_implemented_aborts() and crash-loops the board before
// setup() ever completes (confirmed on real hardware). system_esp32_driver.cpp
// (Task 3) already solved the equivalent problem for its own RAM telemetry
// field with ESP.getFreeHeap(); reuse that here instead of sbrk() on this
// platform.
#if !FW_MCU_ESP32
extern "C" char* sbrk(int incr);
static int freeRamBytes() {
  char top;
  return (int)(&top - (char*)sbrk(0));
}
#else
static int freeRamBytes() {
  return (int)ESP.getFreeHeap();
}
#endif

// Replays everything recorded during setup(). Called at the end of boot (for
// whoever is watching the serial monitor) and again after every `hello` --
// which is what makes boot health visible in the app's Terminal, since the
// host connects long after these lines were produced.
static void emitBootLog() {
  BootLog& b = bootLog();
  for (uint8_t i = 0; i < b.count(); ++i) {
    size_t n = writeLog(g_out, sizeof(g_out), b.line(i));
    if (n > 0) writeLine(g_out, n);
  }
  if (b.dropped()) {
    size_t n = writeLog(g_out, sizeof(g_out), "boot: log full, lines dropped");
    if (n > 0) writeLine(g_out, n);
  }
}

void setup() {
  // First, so the health indicator is alive before anything can fail.
  g_statusLed.begin();

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

  // Microseconds, and first: the pass this stamps is the one just finished,
  // so the reading covers everything below including the telemetry write.
  core::loopStats().mark(micros());

  g_statusLed.tick(now);

  while (Serial.available()) {
    if (g_reader.feed((char)Serial.read())) {
      if (g_reader.overflowed()) {
        static const char kOverflowMsg[] = "{\"ok\":false,\"err\":\"overflow\"}";
        writeLine(kOverflowMsg, sizeof(kOverflowMsg) - 1);
      } else if (g_reader.line()[0] != '\0') {
        Request q = parseRequest(g_reader.line());
        size_t n = g_dispatch.handle(q, g_out, sizeof(g_out));
        if (n > 0) writeLine(g_out, n);
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
    if (n > 0) writeLine(g_out, n);
  }

  if (g_dispatch.telemetryEnabled() && g_tlmRateId != kNoParam) {
    uint32_t period = 1000u / (uint32_t)g_params.num(g_tlmRateId);
    if (period == 0) period = 1;
    if (now - g_lastTlm >= period) {
      g_lastTlm = now;
      g_reg.collectTelemetry(g_tlm);
      size_t n = writeTelemetry(g_out, sizeof(g_out), g_reg, g_tlm);
      if (n > 0) writeLine(g_out, n);
    }
  }
}
