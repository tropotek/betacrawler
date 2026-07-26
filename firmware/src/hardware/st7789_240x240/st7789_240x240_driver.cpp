#include <Arduino.h>
#include <Arduino_GFX_Library.h>
#include <stdio.h>
#include <string.h>

#include "hardware/st7789_240x240/st7789_240x240_driver.h"
#include "core/dispatch.h"      // writeLog
#include "core/tlm_format.h"
#include "core/version.h"
#include "config.h"

#ifndef DISPLAY_DC
#error "FEATURE_ST7789_240X240 is on but the board header defines no DISPLAY_DC"
#endif
#ifndef DISPLAY_RST
#error "FEATURE_ST7789_240X240 is on but the board header defines no DISPLAY_RST"
#endif

// Optional board knobs.
#ifndef DISPLAY_CS
#define DISPLAY_CS GFX_NOT_DEFINED     // GMT130 boards bring no CS out
#endif
#ifndef DISPLAY_ROTATION
#define DISPLAY_ROTATION 0
#endif
#ifndef DISPLAY_W
#define DISPLAY_W 240
#endif
#ifndef DISPLAY_H
#define DISPLAY_H 240
#endif
#ifndef DISPLAY_SPI_HZ
#define DISPLAY_SPI_HZ 8000000
#endif
#ifndef DISPLAY_INIT_BUDGET_MS
#define DISPLAY_INIT_BUDGET_MS 400
#endif

namespace st7789 {

// File-scope statics rather than `new`: this firmware allocates nothing on the
// heap, and the constructors only record pins.
static Arduino_HWSPI   s_bus(DISPLAY_DC, DISPLAY_CS);
static Arduino_ST7789  s_panel(&s_bus, DISPLAY_RST, DISPLAY_ROTATION,
                               true /* IPS */, DISPLAY_W, DISPLAY_H, 0, 0, 0, 0);
static Arduino_GFX*    gfx = &s_panel;

// --- palette (RGB565) --------------------------------------------------------
#define COL_BG      0x0000
#define COL_HEADER  0x0431
#define COL_ACCENT  0x07FF
#define COL_TEXT    0xFFFF
#define COL_LABEL   0x8410
#define COL_DIVIDER 0x2965
#define COL_CLK     0x07FF
#define COL_UP      0xFFE0
#define COL_TEMP    0xFD20
#define COL_VDD     0x07E0
#define COL_RAM     0xFB56
#define COL_GREEN   0x2FEB
#define COL_RED     0xF9A6

// --- layout ------------------------------------------------------------------
enum {
  HEADER_H   = 42,
  BODY_TOP   = HEADER_H + 10,

  // stats page: icon rows, text size 2
  ROW_H      = 22,
  ICON_X     = 10,
  LABEL_X    = 34,
  VALUE_X    = 120,
  DIV_Y      = 186,
  BTN_CX     = 40,
  BTN_CY     = 216,
  BTN_R      = 18,

  // info page: denser, text size 1, so a build date fits on one line
  INFO_ROW_H = 18,
  INFO_LAB_X = 10,
  INFO_VAL_X = 74,
};

enum { ICON_CLOCK = 0, ICON_STOPWATCH, ICON_THERMO, ICON_BOLT, ICON_RAM };

// --- mini icons (16x16 at x,y) -----------------------------------------------
static void iconClock(int x, int y, uint16_t c) {
  gfx->drawCircle(x + 8, y + 8, 7, c);
  gfx->drawLine(x + 8, y + 8, x + 8, y + 3, c);
  gfx->drawLine(x + 8, y + 8, x + 11, y + 9, c);
}
static void iconStopwatch(int x, int y, uint16_t c) {
  gfx->drawCircle(x + 8, y + 9, 6, c);
  gfx->drawFastHLine(x + 6, y + 1, 4, c);
  gfx->drawLine(x + 8, y + 2, x + 8, y + 3, c);
  gfx->drawLine(x + 8, y + 9, x + 8, y + 5, c);
}
static void iconThermo(int x, int y, uint16_t c) {
  gfx->drawRoundRect(x + 5, y, 6, 13, 3, c);
  gfx->fillCircle(x + 8, y + 12, 3, c);
  gfx->fillRect(x + 7, y + 5, 3, 7, c);
}
static void iconBolt(int x, int y, uint16_t c) {
  gfx->fillTriangle(x + 10, y + 1, x + 3, y + 9, x + 9, y + 8, c);
  gfx->fillTriangle(x + 6, y + 15, x + 13, y + 7, x + 7, y + 8, c);
}
static void iconRam(int x, int y, uint16_t c) {
  gfx->drawRoundRect(x + 2, y + 3, 12, 10, 1, c);
  gfx->drawFastVLine(x + 6, y + 3, 10, c);
  gfx->drawFastVLine(x + 10, y + 3, 10, c);
  gfx->drawFastVLine(x + 4, y + 13, 2, c);
  gfx->drawFastVLine(x + 8, y + 13, 2, c);
  gfx->drawFastVLine(x + 12, y + 13, 2, c);
}
static void iconChip(int x, int y, uint16_t c) {
  gfx->fillRoundRect(x + 3, y + 3, 12, 12, 2, c);
  gfx->fillRect(x + 7, y + 6, 4, 6, COL_HEADER);
  for (int i = 0; i < 3; i++) {
    gfx->drawFastHLine(x + 0, y + 5 + i * 3, 3, c);
    gfx->drawFastHLine(x + 15, y + 5 + i * 3, 3, c);
    gfx->drawFastVLine(x + 5 + i * 3, y + 0, 3, c);
    gfx->drawFastVLine(x + 5 + i * 3, y + 15, 3, c);
  }
}

void St7789Driver::drawRowIcon(uint8_t icon, int y, uint16_t c) {
  switch (icon) {
    case ICON_CLOCK:     iconClock(ICON_X, y, c); break;
    case ICON_STOPWATCH: iconStopwatch(ICON_X, y, c); break;
    case ICON_THERMO:    iconThermo(ICON_X, y, c); break;
    case ICON_BOLT:      iconBolt(ICON_X, y, c); break;
    case ICON_RAM:       iconRam(ICON_X, y, c); break;
  }
}

// Background-filled text: overwrites the previous value in place, so a steady
// refresh costs a few hundred bytes of SPI instead of a full repaint, and
// never flickers. This is why no framebuffer is needed (240x240x2 = 115KB
// would not fit in 128KB of RAM anyway).
void St7789Driver::setValue(int x, int y, const char* text, uint16_t colour,
                             uint8_t size) {
  gfx->setTextSize(size);
  gfx->setTextColor(colour, COL_BG);
  gfx->setCursor(x, y);
  gfx->print(text);
}

// --- lifecycle ---------------------------------------------------------------

void St7789Driver::attach(const core::Registry& reg, const core::Params& p) {
  reg_ = &reg;
  params_ = &p;

  // Resolve everything the curated layout wants, once. Anything this board
  // does not publish stays kNoParam/0xFF and drops out of the layout.
  reg.findParam("device.name", &pName_);
  reg.findParam("led.mode", &pLedMode_);
  reg.findParam("led.blink_hz", &pLedRate_);
  reg.findTlm("btn", &tBtn_);

  static const struct { const char* key; const char* label; uint16_t colour;
                        uint8_t icon; bool uptime; } kWanted[] = {
    {"clk",  "CLK",   COL_CLK,  ICON_CLOCK,     false},
    {"up",   "UP",    COL_UP,   ICON_STOPWATCH, true},
    {"temp", "TEMP",  COL_TEMP, ICON_THERMO,    false},
    {"vdd",  "VDD",   COL_VDD,  ICON_BOLT,      false},
    {"ram",  "RAM",   COL_RAM,  ICON_RAM,       false},
  };
  rowCount_ = 0;
  for (size_t i = 0; i < sizeof(kWanted) / sizeof(kWanted[0]); ++i) {
    uint8_t idx = 0;
    if (!reg.findTlm(kWanted[i].key, &idx)) continue;   // absent: row closes up
    rows_[rowCount_++] = Row{idx, kWanted[i].label, kWanted[i].colour,
                             kWanted[i].icon, kWanted[i].uptime};
  }

  // Our own parameters are already loaded from flash by the time attach()
  // runs (main.cpp: store.load() precedes reg.begin()), so begin() can act on
  // the persisted mode rather than the compiled-in default.
  mode_ = p.num(globalParam(P_MODE));
  page_ = p.num(globalParam(P_PAGE));
  rate_ = p.num(globalParam(P_RATE));
  shownPage_ = (page_ == PAGE_CYCLE) ? PAGE_INFO : page_;
}

void St7789Driver::logInit(uint32_t elapsedMs) {
  char msg[128];
  snprintf(msg, sizeof(msg),
           "display: ST7789 %dx%d dc=%d rst=%d init=%lums%s",
           (int)DISPLAY_W, (int)DISPLAY_H, (int)DISPLAY_DC, (int)DISPLAY_RST,
           (unsigned long)elapsedMs,
           elapsedMs > DISPLAY_INIT_BUDGET_MS ? " WARN slow-init" : "");

  // The honest diagnostic. There is no MISO and no CS on this wiring, and
  // Arduino_HWSPI::begin() returns true unconditionally, so the panel's
  // presence CANNOT be detected -- reporting "not connected" would be a
  // fabrication. What this line does say is true: the module ran, with these
  // pins, in this long. A blank screen alongside it means wiring, not
  // firmware.
  char line[192];
  size_t n = core::writeLog(line, sizeof(line), msg);
  if (n > 0) Serial.println(line);
}

void St7789Driver::initHardware() {
  if (inited_) return;
  uint32_t t0 = millis();
  gfx->begin(DISPLAY_SPI_HZ);
  uint32_t elapsed = millis() - t0;
  inited_ = true;
  logInit(elapsed);

  // Splash, so "is it alive" is answerable at a glance on the panel itself.
  gfx->fillScreen(COL_BG);
  gfx->setTextSize(2);
  gfx->setTextColor(COL_ACCENT);
  gfx->setCursor(10, 100);
  gfx->print(core::projectName());
  gfx->setTextSize(1);
  gfx->setTextColor(COL_LABEL);
  gfx->setCursor(10, 126);
  gfx->print(core::version());
  gfx->setCursor(10, 140);
  gfx->print(core::boardId());
  delay(800);

  repaint();
}

void St7789Driver::begin() {
  // Lazy: a board whose panel is absent or broken can be left dark from the
  // web UI (disp.mode = off, save) and then costs nothing at all -- no SPI, no
  // pin ownership, and none of the ST7789 reset sequence's ~130-260ms of
  // delay() in setup().
  if (mode_ == MODE_OFF) return;
  initHardware();
}

void St7789Driver::onParamChanged(uint8_t local, const core::Params& p) {
  (void)local;
  const int32_t prevMode = mode_;
  const int32_t prevPage = page_;
  mode_ = p.num(globalParam(P_MODE));
  page_ = p.num(globalParam(P_PAGE));
  rate_ = p.num(globalParam(P_RATE));

  if (mode_ == MODE_OFF) {
    if (inited_ && prevMode != MODE_OFF) gfx->fillScreen(COL_BG);
    return;
  }
  if (!inited_) { initHardware(); return; }   // deferred init, first off->on
  if (prevMode == MODE_OFF || page_ != prevPage) {
    shownPage_ = (page_ == PAGE_CYCLE) ? PAGE_INFO : page_;
    repaint();
  }
}

int32_t St7789Driver::livePage() const {
  return page_ == PAGE_CYCLE ? shownPage_ : page_;
}

void St7789Driver::tick(uint32_t nowMs) {
  if (mode_ == MODE_OFF || !inited_) return;   // off costs nothing

  if (page_ == PAGE_CYCLE && nowMs - lastFlip_ >= kCycleMs) {
    lastFlip_ = nowMs;
    shownPage_ = (shownPage_ == PAGE_INFO) ? PAGE_STATS : PAGE_INFO;
    repaint();          // the only routine full repaint, once per kCycleMs
    lastDraw_ = nowMs;
    return;
  }

  uint32_t period = 1000u / (uint32_t)(rate_ < 1 ? 1 : rate_);
  if (nowMs - lastDraw_ < period) return;
  lastDraw_ = nowMs;

  drawHeader();
  if (livePage() == PAGE_STATS) drawStatsValues();
  else                          drawInfoValues();
}

// --- painting ----------------------------------------------------------------

void St7789Driver::repaint() {
  gfx->fillRect(0, HEADER_H, DISPLAY_W, DISPLAY_H - HEADER_H, COL_BG);
  drawHeader();
  if (livePage() == PAGE_STATS) { drawStatsStatic(); drawStatsValues(); }
  else                          { drawInfoStatic();  drawInfoValues(); }
}

void St7789Driver::drawHeader() {
  gfx->fillRect(0, 0, DISPLAY_W, HEADER_H, COL_HEADER);
  iconChip(10, 6, COL_ACCENT);

  gfx->setTextSize(2);
  gfx->setTextColor(COL_TEXT);
  gfx->setCursor(34, 6);
  gfx->print(pName_ != core::kNoParam ? params_->str(pName_)
                                      : core::projectName());

  char sub[48];
  snprintf(sub, sizeof(sub), "%s %s %s", core::boardId(), core::projectName(),
           core::version());
  gfx->setTextSize(1);
  gfx->setTextColor(COL_ACCENT);
  gfx->setCursor(34, 27);
  gfx->print(sub);

  // Heartbeat: the cheapest possible "the loop is still running" signal.
  beat_ = !beat_;
  gfx->fillCircle(228, 12, 4, beat_ ? COL_ACCENT : COL_HEADER);
}

// The info page answers the todo directly: name, firmware version, and the
// LED's mode and rate.
static const char* const kInfoLabels[] = {"FW", "BUILT", "BOARD", "MODS", "LED"};
enum { I_FW = 0, I_BUILT, I_BOARD, I_MODS, I_LED, I_COUNT };

void St7789Driver::drawInfoStatic() {
  gfx->setTextSize(1);
  for (int i = 0; i < I_COUNT; ++i) {
    gfx->setTextColor(COL_LABEL, COL_BG);
    gfx->setCursor(INFO_LAB_X, BODY_TOP + i * INFO_ROW_H);
    gfx->print(kInfoLabels[i]);
  }
}

void St7789Driver::drawInfoValues() {
  char buf[64];

  snprintf(buf, sizeof(buf), "%s %s", core::projectName(), core::version());
  setValue(INFO_VAL_X, BODY_TOP + I_FW * INFO_ROW_H, buf, COL_TEXT, 1);

  setValue(INFO_VAL_X, BODY_TOP + I_BUILT * INFO_ROW_H, core::buildDate(),
           COL_TEXT, 1);
  setValue(INFO_VAL_X, BODY_TOP + I_BOARD * INFO_ROW_H, core::boardId(),
           COL_TEXT, 1);

  // The module list is the one genuinely registry-derived thing on the page,
  // and it is worth it: it says exactly which modules this build has.
  buf[0] = '\0';
  for (uint8_t i = 0; i < reg_->moduleCount(); ++i) {
    if (i) strncat(buf, " ", sizeof(buf) - strlen(buf) - 1);
    strncat(buf, reg_->moduleId(i), sizeof(buf) - strlen(buf) - 1);
  }
  setValue(INFO_VAL_X, BODY_TOP + I_MODS * INFO_ROW_H, buf, COL_ACCENT, 1);

  // Absent LED module: say so rather than leaving a stale or blank row.
  if (pLedMode_ != core::kNoParam) {
    const core::ParamDef& d = reg_->paramDef(pLedMode_);
    const int32_t m = params_->num(pLedMode_);
    const char* name = (d.options && m >= 0 && m < d.optionCount)
                           ? d.options[m] : "?";
    if (pLedRate_ != core::kNoParam)
      snprintf(buf, sizeof(buf), "%s @ %ld Hz", name,
               (long)params_->num(pLedRate_));
    else
      snprintf(buf, sizeof(buf), "%s", name);
  } else {
    snprintf(buf, sizeof(buf), "--");
  }
  setValue(INFO_VAL_X, BODY_TOP + I_LED * INFO_ROW_H, buf, COL_TEXT, 1);
}

void St7789Driver::drawStatsStatic() {
  gfx->setTextSize(2);
  for (uint8_t i = 0; i < rowCount_; ++i) {
    int y = BODY_TOP + i * ROW_H;
    drawRowIcon(rows_[i].icon, y, rows_[i].colour);
    gfx->setTextColor(COL_LABEL, COL_BG);
    gfx->setCursor(LABEL_X, y);
    gfx->print(rows_[i].label);
  }
  if (tBtn_ != 0xFF) {
    gfx->drawFastHLine(6, DIV_Y, 228, COL_DIVIDER);
    gfx->setTextSize(1);
    gfx->setTextColor(COL_LABEL, COL_BG);
    gfx->setCursor(80, DIV_Y + 8);
    gfx->print("USER BUTTON");
    drawButton(false);
  }
}

void St7789Driver::drawStatsValues() {
  core::TlmValue vals[FW_MAX_TLM];
  reg_->collectTelemetry(vals);

  char buf[32];
  for (uint8_t i = 0; i < rowCount_; ++i) {
    const Row& r = rows_[i];
    if (r.uptime) core::formatUptime(vals[r.tlm].u, buf, sizeof(buf));
    else core::formatTlm(reg_->tlmDef(r.tlm), vals[r.tlm], buf, sizeof(buf));
    // Trailing spaces cover a value that just got shorter; background-filled
    // text only paints the glyphs it draws.
    strncat(buf, "   ", sizeof(buf) - strlen(buf) - 1);
    setValue(VALUE_X, BODY_TOP + i * ROW_H, buf, r.colour, 2);
  }

  if (tBtn_ != 0xFF) {
    bool pressed = vals[tBtn_].u != 0;
    if (pressed != btnState_) {
      btnState_ = pressed;
      drawButton(pressed);
      gfx->setTextSize(2);
      gfx->setTextColor(pressed ? COL_GREEN : COL_LABEL, COL_BG);
      gfx->setCursor(80, BTN_CY - 6);
      gfx->print(pressed ? "PRESSED " : "RELEASED");
    }
  }
}

void St7789Driver::drawButton(bool pressed) {
  if (pressed) {
    gfx->fillCircle(BTN_CX, BTN_CY, BTN_R, COL_GREEN);
    gfx->fillCircle(BTN_CX, BTN_CY, BTN_R - 6, 0x1D66);
  } else {
    gfx->fillCircle(BTN_CX, BTN_CY, BTN_R, COL_BG);
    gfx->drawCircle(BTN_CX, BTN_CY, BTN_R, COL_ACCENT);
    gfx->fillCircle(BTN_CX, BTN_CY, BTN_R - 7, 0x2124);
    gfx->drawCircle(BTN_CX, BTN_CY, BTN_R - 7, COL_ACCENT);
  }
}

}  // namespace st7789
