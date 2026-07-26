#pragma once
#include "hardware/st7789_240x240/st7789_240x240_params.h"
#include "core/registry.h"

// Forward-declared so this header does not drag Arduino_GFX into every
// translation unit that includes it (modules.cpp, notably).
class Arduino_GFX;

namespace st7789 {

// Requires DISPLAY_DC and DISPLAY_RST from the board header; DISPLAY_CS,
// DISPLAY_ROTATION, DISPLAY_SPI_HZ and DISPLAY_INIT_BUDGET_MS are optional
// and defaulted in the .cpp.
//
// The layout is curated rather than derived from the registry: it has to fit
// 240x240 exactly, and a board may want to show things the registry knows
// nothing about. The coupling that buys is contained -- every key is resolved
// once in attach(), and a key this board does not publish simply drops its
// row, so FEATURE_LED 0 or a buttonless board still renders correctly.
class St7789Driver : public core::Module {
 public:
  void attach(const core::Registry& reg, const core::Params& p) override;
  void begin() override;
  void tick(uint32_t nowMs) override;
  void onParamChanged(uint8_t local, const core::Params& p) override;

 private:
  // One stats row: a resolved telemetry index plus its chrome. Built in
  // attach() from whatever this board actually publishes.
  struct Row {
    uint8_t     tlm;      // global telemetry index
    const char* label;
    uint16_t    colour;
    uint8_t     icon;     // ICON_* selector
    bool        uptime;   // render as HH:MM:SS rather than via the descriptor
  };

  void initHardware();
  void repaint();                  // full: chrome + values
  void drawHeaderStatic();         // the bar itself -- only on a full repaint
  void drawHeaderLive();           // name + heartbeat -- every refresh
  void drawInfoStatic();
  void drawInfoValues();
  void drawStatsStatic();
  void drawStatsValues();
  void drawButton(bool pressed);
  void drawRowIcon(uint8_t icon, int y, uint16_t colour);
  void setValue(int x, int y, const char* text, uint16_t colour, uint8_t size,
                uint16_t bg = 0x0000);
  // Returns false when `text` is already what slot `i` shows, so an unchanged
  // value costs no SPI at all. Measured on hardware: without this a refresh
  // delayed a telemetry frame by ~87ms, enough to trip the frontend's 3x
  // staleness watchdog at tlm.rate 50.
  bool changed(uint8_t slot, const char* text);
  void logInit(uint32_t elapsedMs);
  int32_t livePage() const;        // resolves PAGE_CYCLE to what is shown now

  const core::Registry* reg_    = nullptr;
  const core::Params*   params_ = nullptr;

  // Resolved once in attach(); kNoParam / 0xFF when this board lacks them.
  core::ParamId pName_    = core::kNoParam;
  core::ParamId pLedMode_ = core::kNoParam;
  core::ParamId pLedRate_ = core::kNoParam;
  uint8_t       tBtn_     = 0xFF;

  Row    rows_[6];
  uint8_t rowCount_ = 0;

  int32_t  mode_ = MODE_ON;
  int32_t  page_ = PAGE_INFO;
  int32_t  rate_ = 2;

  // Last text painted into each value slot, so an unchanged one is skipped.
  // Slots are page-local; repaint() clears them, since a page switch makes
  // whatever was there meaningless.
  static const uint8_t kSlots = 8;
  char     shown_[kSlots][28] = {};

  static const uint32_t kSplashMs = 800;
  uint32_t splashUntil_ = 0;     // non-zero while the splash is still up

  bool     inited_    = false;   // gfx->begin() has run
  int32_t  shownPage_ = PAGE_INFO;
  uint32_t lastDraw_  = 0;
  uint32_t lastFlip_  = 0;
  bool     beat_      = false;
  bool     btnState_  = false;
};

}  // namespace st7789
