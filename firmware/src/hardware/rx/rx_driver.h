#pragma once
// As of 2026-07-27: the UART receive path (drainUart/FrameParser fed from a
// real HardwareSerial) has never received a byte from an actual CRSF
// receiver -- no Crossfire receiver or 420000-baud adapter was on the bench
// during development. The protocol layer (crc8, FrameParser, unpackChannels,
// ticksToUs, decodeLinkStats, LinkState) is natively tested against known-good
// frames and is not in question here. What HAS been exercised on a board is
// the `sim` source -- synthetic channels, the telemetry frame, the schema,
// and the bar rendering. Treat the uart path as built, not verified.
#include "hardware/rx/rx_params.h"
#include "core/inputs.h"

// Forward-declared rather than including <HardwareSerial.h>: this header is
// pulled in by modules.cpp, and the Arduino serial header is heavy.
class HardwareSerial;

namespace rx {

// Requires RX_RX_PIN, RX_TX_PIN and RX_BAUD from the board header.
class RxDriver : public core::Module {
 public:
  // `inputs` is the shared bus this driver publishes decoded channels to --
  // wired in modules.cpp, not resolved through attach()'s const Registry,
  // because attach() deliberately never hands out a writable reference to
  // anything (core::Module's "observers are const" rule, core/module.h).
  // See _notes/spec-rx-mapping.md.
  explicit RxDriver(core::Inputs& inputs) : inputs_(inputs) {}

  // Seeds source_/timeoutMs_ from the loaded params. Runs on every module
  // before any module's begin() (see core::Module::attach), which is what
  // lets begin()'s "source == sim" check see a flash-persisted sim setting
  // rather than only ever the SRC_UART member default: onParamChanged() is
  // not called during the initial load, only on a later change. Access
  // stays const -- this only mirrors state locally, it reconfigures nothing.
  void attach(const core::Registry& reg, const core::Params& p) override;
  void begin() override;
  void tick(uint32_t nowMs) override;
  void onParamChanged(uint8_t local, const core::Params& p) override;
  void readTelemetry(core::TlmValue* out) override;

 private:
  void drainUart(uint32_t nowMs);
  void runSim(uint32_t nowMs);
  void applyRcFrame(uint32_t nowMs);
  void syncInputs();

  const Protocol& proto() const { return kProtocols[protocol_]; }

  core::Inputs&   inputs_;
  HardwareSerial* uart_ = nullptr;
  FrameParser     parser_;
  LinkState       link_;
  LinkStats       stats_ = {};
  // True once a 0x14 link-statistics frame has been decoded since the last
  // reset (source/protocol change, or runSim's synthetic stats). link_.up()
  // alone is not enough to trust stats_: it flips true on the first 0x16 RC
  // frame, which can arrive before any 0x14 ever has, and rfMode==0 is a
  // valid table index rather than an out-of-range sentinel -- so reading
  // stats_ on link state alone can publish a rate the receiver never sent.
  bool            statsSeen_ = false;
  uint16_t        us_[kWireChannels] = {};
  int32_t         protocol_  = PROTO_CROSSFIRE;
  int32_t         source_    = SRC_UART;
  // Mirrors the ACTIVE protocol's timeout param. Re-read whenever either the
  // protocol or that protocol's own timeout changes.
  uint32_t        timeoutMs_ = 1000;
  uint16_t        deadbandUs_ = 0;
  uint32_t        simT0_     = 0;
  // Last simulated-frame timestamp, gating runSim()'s link_.onFrame() to a
  // realistic ~150fps cadence (see runSim). Zero-initialized on purpose: a
  // zero start makes the first sim frame fire immediately rather than
  // waiting out the throttle window.
  uint32_t        lastSimFrameMs_ = 0;
};

}  // namespace rx
