#include "hardware/crsf/crsf_driver.h"
#include "core/boot_log.h"
#include "core/led_curve.h"
#include <Arduino.h>
#include <HardwareSerial.h>
#include "config.h"

#ifndef CRSF_RX_PIN
#error "FEATURE_CRSF is on but the board header defines no CRSF_RX_PIN"
#endif
#ifndef CRSF_TX_PIN
#error "FEATURE_CRSF is on but the board header defines no CRSF_TX_PIN"
#endif
#ifndef CRSF_BAUD
#error "FEATURE_CRSF is on but the board header defines no CRSF_BAUD"
#endif

// A CRSF frame lands every 6.6ms at 150fps; the Arduino default RX ring (64
// bytes) holds ~2 frames, and a display refresh alone (measured 87ms) will
// tear the stream long before that. 256 is the core's hard cap and the
// requirement this module actually has -- see platformio.ini's build_flags
// for why it lives there and not just in this comment.
#if SERIAL_RX_BUFFER_SIZE < 256
#error "FEATURE_CRSF needs -D SERIAL_RX_BUFFER_SIZE=256 on this env's build_flags"
#endif

namespace crsf {

// Constructed from the pins rather than using a global Serial1: the STM32
// core only defines Serial1 when the VARIANT declares PIN_SERIAL1_RX/TX,
// which is not something a board header controls. Constructing from pins lets
// the core resolve the peripheral from its own pin map, and keeps the wiring
// stated in exactly one place -- the board header.
static HardwareSerial g_uart(CRSF_RX_PIN, CRSF_TX_PIN);

void CrsfDriver::attach(const core::Registry& reg, const core::Params& p) {
  (void)reg;
  // Params reflects whatever main.cpp's store.load() already restored from
  // flash, so this is real persisted state, not the SRC_UART member default
  // -- source_ would otherwise stay SRC_UART inside begin() even when
  // crsf.source=sim was saved, because onParamChanged() only fires on a
  // LATER change, never for the initial load.
  source_    = p.num(globalParam(P_SOURCE));
  timeoutMs_ = (uint32_t)p.num(globalParam(P_TIMEOUT_MS));
}

void CrsfDriver::begin() {
  uart_ = &g_uart;
  uart_->begin(CRSF_BAUD);
  for (uint8_t i = 0; i < kUsedChannels; ++i) us_[i] = 0;
  if (source_ == SRC_SIM)
    core::bootLog().add("CRSF source=sim (synthetic channels, no receiver)");
}

void CrsfDriver::onParamChanged(uint8_t local, const core::Params& p) {
  switch (local) {
    case P_SOURCE: {
      const int32_t v = p.num(globalParam(P_SOURCE));
      if (v != source_) {
        // Switching away from sim must not leave the last synthetic frame
        // on display forever -- ch1..12 would otherwise keep drawing bars
        // from invented data even after the link genuinely times out, which
        // is the exact fabricated-data failure crsf.source defaulting to
        // uart exists to prevent, just reached by a different route.
        for (uint8_t i = 0; i < kUsedChannels; ++i) us_[i] = 0;
        stats_ = LinkStats{};
        simT0_ = 0;
        // link_ carries sim's "up, rate ~143" across the switch unless reset
        // too -- a board with nothing wired to uart would otherwise keep
        // publishing a fabricated link for up to timeoutMs after the switch.
        // err_ survives inside reset(): a real rejection stays countable
        // across a source change.
        link_.reset();
        // Any mid-frame state from the other source is now meaningless; a
        // fresh parser avoids costing one extra rejection on the next
        // uart -> sim -> uart round trip. FrameParser has no reset() of its
        // own, so a fresh instance stands in for one.
        parser_ = FrameParser{};
      }
      source_ = v;
      break;
    }
    case P_TIMEOUT_MS:
      timeoutMs_ = (uint32_t)p.num(globalParam(P_TIMEOUT_MS));
      break;
    default:
      break;
  }
}

void CrsfDriver::tick(uint32_t nowMs) {
  if (source_ == SRC_SIM) {
    runSim(nowMs);
    return;
  }
  drainUart(nowMs);
  link_.tick(nowMs, timeoutMs_);
}

void CrsfDriver::drainUart(uint32_t nowMs) {
  if (!uart_) return;
  // EVERY available byte, not a fixed number per loop: at 150 frames/s the
  // 256-byte RX ring holds ~66ms of stream and a display refresh has been
  // measured at 87ms. Draining a fixed quota would guarantee the buffer wins.
  while (uart_->available() > 0) {
    const uint8_t b = (uint8_t)uart_->read();
    switch (parser_.feed(b)) {
      case FrameParser::Result::Frame:
        if (parser_.type() == kTypeRcChannels &&
            parser_.payloadLen() == kRcPayloadLen) {
          applyRcFrame(nowMs);
        } else if (parser_.type() == kTypeLinkStats &&
                   parser_.payloadLen() == kLinkPayloadLen) {
          decodeLinkStats(parser_.payload(), &stats_);
        }
        // Any other type is a well-formed frame this module does not consume.
        break;
      case FrameParser::Result::Rejected:
        link_.onReject();
        break;
      case FrameParser::Result::None:
        break;
    }
  }
}

void CrsfDriver::applyRcFrame(uint32_t nowMs) {
  uint16_t ticks[kWireChannels] = {};
  unpackChannels(parser_.payload(), ticks);
  // Only the first twelve: Crossfire transmits 12 and the rest are padding.
  for (uint8_t i = 0; i < kUsedChannels; ++i) us_[i] = ticksToUs(ticks[i]);
  link_.onFrame(nowMs);
}

void CrsfDriver::runSim(uint32_t nowMs) {
  // Synthetic channels so the telemetry frame, the schema, the grouping and
  // the bar rendering can all be exercised with nothing but a USB cable.
  // These values are FABRICATED and the module says so in the boot log; the
  // standing safeguard is that crsf.source is itself a visible parameter.
  if (simT0_ == 0) simT0_ = nowMs;
  const uint32_t t = nowMs - simT0_;

  // breathingDuty is the same symmetric triangle the servo sweep uses: 0..100
  // over the period. Reusing it keeps one curve in the firmware, tested once.
  const uint16_t span = 2012 - 988;
  us_[0] = (uint16_t)(988 + (uint32_t)core::breathingDuty(t % 4000, 4000) * span / 100);
  us_[1] = (uint16_t)(988 + (uint32_t)core::breathingDuty(t % 8000, 8000) * span / 100);
  us_[2] = ((t / 2000) % 2) ? 2012 : 988;          // switch-like input
  for (uint8_t i = 3; i < kUsedChannels; ++i)
    us_[i] = (uint16_t)(988 + span * (i - 2) / (kUsedChannels - 2));

  stats_.lq      = 100;
  stats_.rssiDbm = -42;
  stats_.snr     = 12;
  stats_.antenna = 0;
  // Report a healthy link at a plausible rate WITHOUT touching the error
  // count: a real rejection stays countable even here. Throttled to ~7ms
  // (~143Hz, a real Crossfire profile rate) rather than calling onFrame()
  // once per tick(): tick() runs on every unthrottled loop() iteration --
  // thousands of times a second on a 100MHz F411 -- so an unthrottled call
  // would make LinkState::rate() report loop frequency, not a frame rate.
  // Unsigned subtraction, matching LinkState's own wraparound convention.
  if ((uint32_t)(nowMs - lastSimFrameMs_) >= 7) {
    link_.onFrame(nowMs);
    lastSimFrameMs_ = nowMs;
  }
  link_.tick(nowMs, timeoutMs_);
}

void CrsfDriver::readTelemetry(core::TlmValue* out) {
  for (uint8_t i = 0; i < kUsedChannels; ++i) out[T_CH1 + i].u = us_[i];
  const bool up = link_.up();
  out[T_LINK].u = up ? 1u : 0u;
  // Stale link statistics are worse than none: a frozen "LQ 100" beside
  // "Link 0" reads as a working link. They zero with the link instead.
  out[T_LQ].u   = up ? stats_.lq : 0u;
  out[T_RSSI].i = up ? stats_.rssiDbm : 0;
  out[T_RATE].u = link_.rate();
  out[T_ERR].u  = link_.errors();
}

}  // namespace crsf
