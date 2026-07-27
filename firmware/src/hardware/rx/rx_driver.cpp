#include "hardware/rx/rx_driver.h"
#include "core/boot_log.h"
#include "core/led_curve.h"
#include <Arduino.h>
#include <HardwareSerial.h>
#include "config.h"

#ifndef RX_RX_PIN
#error "FEATURE_RX is on but the board header defines no RX_RX_PIN"
#endif
#ifndef RX_TX_PIN
#error "FEATURE_RX is on but the board header defines no RX_TX_PIN"
#endif
#ifndef RX_BAUD
#error "FEATURE_RX is on but the board header defines no RX_BAUD"
#endif

// A CRSF frame lands every 6.6ms at 150fps; the Arduino default RX ring (64
// bytes) holds ~2 frames, and a display refresh alone (measured 87ms) will
// tear the stream long before that. 256 is the core's hard cap and the
// requirement this module actually has -- see platformio.ini's build_flags
// for why it lives there and not just in this comment.
#if SERIAL_RX_BUFFER_SIZE < 256
#error "FEATURE_RX needs -D SERIAL_RX_BUFFER_SIZE=256 on this env's build_flags"
#endif

namespace rx {

// Constructed from the pins rather than using a global Serial1: the STM32
// core only defines Serial1 when the VARIANT declares PIN_SERIAL1_RX/TX,
// which is not something a board header controls. Constructing from pins lets
// the core resolve the peripheral from its own pin map, and keeps the wiring
// stated in exactly one place -- the board header.
static HardwareSerial g_uart(RX_RX_PIN, RX_TX_PIN);

void RxDriver::attach(const core::Registry& reg, const core::Params& p) {
  (void)reg;
  // Params reflects whatever main.cpp's store.load() already restored from
  // flash, so this is real persisted state. onParamChanged() only fires on a
  // LATER change, never for the initial load -- without this, a saved
  // rx.protocol=elrs would sit unread behind the member default until the
  // user happened to touch the control.
  protocol_  = p.num(globalParam(P_PROTOCOL));
  source_    = p.num(globalParam(P_SOURCE));
  timeoutMs_ = (uint32_t)p.num(globalParam(proto().timeoutParam));
}

void RxDriver::begin() {
  uart_ = &g_uart;
  uart_->begin(proto().baud);
  for (uint8_t i = 0; i < kWireChannels; ++i) us_[i] = 0;
  if (source_ == SRC_SIM)
    core::bootLog().add("RX source=sim (synthetic channels, no receiver)");
}

void RxDriver::onParamChanged(uint8_t local, const core::Params& p) {
  switch (local) {
    case P_PROTOCOL: {
      const int32_t v = p.num(globalParam(P_PROTOCOL));
      if (v != protocol_) {
        const uint32_t wasBaud = proto().baud;
        protocol_ = v;
        // Everything decoded under the previous protocol is now meaningless.
        // Leaving it on screen is the same fabricated-data failure that
        // rx.source defaulting to uart exists to prevent, reached by another
        // route: channel values, link statistics and the link state itself
        // all described a protocol that is no longer selected.
        for (uint8_t i = 0; i < kWireChannels; ++i) us_[i] = 0;
        stats_ = LinkStats{};
        simT0_ = 0;
        // err_ survives inside reset(): a real rejection stays countable
        // across a protocol change, exactly as across a source change.
        link_.reset();
        // FrameParser has no reset() of its own; a fresh instance stands in.
        parser_ = FrameParser{};
        if (uart_ && proto().baud != wasBaud) {
          uart_->end();
          uart_->begin(proto().baud);
        }
      }
      // Always re-read: the newly active protocol owns a different param.
      timeoutMs_ = (uint32_t)p.num(globalParam(proto().timeoutParam));
      break;
    }
    case P_SOURCE: {
      const int32_t v = p.num(globalParam(P_SOURCE));
      if (v != source_) {
        // Switching away from sim must not leave the last synthetic frame
        // on display forever -- ch1..12 would otherwise keep drawing bars
        // from invented data even after the link genuinely times out, which
        // is the exact fabricated-data failure rx.source defaulting to
        // uart exists to prevent, just reached by a different route.
        for (uint8_t i = 0; i < kWireChannels; ++i) us_[i] = 0;
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
    case P_CROSSFIRE_TIMEOUT:
    case P_ELRS_TIMEOUT:
      // Accepted and stored either way; only the active one takes effect.
      // That is the direct consequence of showIf being display-only -- an INI
      // restore sets both, and neither may be refused.
      timeoutMs_ = (uint32_t)p.num(globalParam(proto().timeoutParam));
      break;
    default:
      break;
  }
}

void RxDriver::tick(uint32_t nowMs) {
  if (source_ == SRC_SIM) {
    runSim(nowMs);
    return;
  }
  drainUart(nowMs);
  link_.tick(nowMs, timeoutMs_);
}

void RxDriver::drainUart(uint32_t nowMs) {
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

void RxDriver::applyRcFrame(uint32_t nowMs) {
  uint16_t ticks[kWireChannels] = {};
  unpackChannels(parser_.payload(), ticks);
  // Only what this protocol actually transmits. Crossfire pads slots 13-16,
  // and publishing padding as though it were a stick position is exactly the
  // fabricated data rx.source=uart exists to avoid.
  const uint8_t n = proto().channels;
  for (uint8_t i = 0; i < n; ++i) us_[i] = ticksToUs(ticks[i]);
  for (uint8_t i = n; i < kWireChannels; ++i) us_[i] = 0;
  link_.onFrame(nowMs);
}

void RxDriver::runSim(uint32_t nowMs) {
  // Synthetic channels so the telemetry frame, the schema, the grouping and
  // the bar rendering can all be exercised with nothing but a USB cable.
  // These values are FABRICATED and the module says so in the boot log; the
  // standing safeguard is that rx.source is itself a visible parameter.
  if (simT0_ == 0) simT0_ = nowMs;
  const uint32_t t = nowMs - simT0_;

  // breathingDuty is the same symmetric triangle the servo sweep uses: 0..100
  // over the period. Reusing it keeps one curve in the firmware, tested once.
  const uint16_t span = 2012 - 988;
  us_[0] = (uint16_t)(988 + (uint32_t)core::breathingDuty(t % 4000, 4000) * span / 100);
  us_[1] = (uint16_t)(988 + (uint32_t)core::breathingDuty(t % 8000, 8000) * span / 100);
  us_[2] = ((t / 2000) % 2) ? 2012 : 988;          // switch-like input
  const uint8_t n = proto().channels;
  for (uint8_t i = 3; i < n; ++i)
    us_[i] = (uint16_t)(988 + span * (i - 2) / (n - 2));
  // sim must not be the one place a Crossfire link appears to carry 16
  // channels -- it exists to exercise the real rendering path, not a
  // friendlier one.
  for (uint8_t i = n; i < kWireChannels; ++i) us_[i] = 0;

  stats_.lq      = 100;
  stats_.rssiDbm = -42;
  stats_.snr     = 12;
  stats_.antenna = 0;
  // Index 2: 150Hz under Crossfire, 200Hz under ELRS -- the sim deliberately
  // reads differently per protocol, so a broken selector is visible without a
  // receiver.
  stats_.rfMode  = 2;
  stats_.txPower = 3;   // 100mW in CRSF's shared table
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

void RxDriver::readTelemetry(core::TlmValue* out) {
  for (uint8_t i = 0; i < kWireChannels; ++i) out[T_CH1 + i].u = us_[i];
  const bool up = link_.up();
  out[T_LINK].u = up ? 1u : 0u;
  // Stale link statistics are worse than none: a frozen "LQ 100" beside
  // "Link 0" reads as a working link. They zero with the link instead.
  out[T_LQ].u   = up ? stats_.lq : 0u;
  out[T_RSSI].i = up ? stats_.rssiDbm : 0;
  out[T_RATE].u = link_.rate();
  out[T_ERR].u  = link_.errors();
  // Zeroed with the link, like lq and rssi: a frozen "500 Hz, 250 mW" beside
  // "Link 0" reads as a working link, which is worse than no reading at all.
  out[T_RFRATE].u = up ? proto().rfRateHz(stats_.rfMode) : 0u;
  out[T_PWR].u    = up ? txPowerMw(stats_.txPower) : 0u;
}

}  // namespace rx
