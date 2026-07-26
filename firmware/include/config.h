#pragma once
// Project-level firmware configuration.
//
// This is the one file to edit when forking app-demo into a real project:
// name, version, serial speed and the capacity limits below. Which *hardware*
// exists is not decided here -- that lives in the board header, included at
// the bottom.

// --- project identity -------------------------------------------------------
#define FW_PROJECT_NAME "app-demo"
// app-demo is a template. It stays at 1.0.0 forever; a fork bumps this. The
// app (backend + web UI) is versioned separately in app/web/app.js -- the two
// are unrelated projects and their numbers are not meant to track each other.
#define FW_VERSION      "1.0.0"

// --- link -------------------------------------------------------------------
// Must match `monitor_speed` in platformio.ini. There is no way to share one
// value between a C header and an ini file, so they are kept in sync by hand;
// a mismatch shows up immediately as garbage in the serial monitor.
#define FW_SERIAL_BAUD  115200

// --- capacity limits --------------------------------------------------------
// Static ceilings for the module registry. Everything is fixed-size: no
// malloc anywhere in this firmware. Raising these costs RAM
// (FW_MAX_PARAMS * sizeof(core::Value) is the big one, 36 bytes per slot),
// which is cheap on a 128KB part. Registry::add() refuses to exceed them
// rather than overflowing, and a native test covers that path.
#define FW_MAX_MODULES  8
#define FW_MAX_PARAMS   32
#define FW_MAX_TLM      16

// --- board ------------------------------------------------------------------
// BOARD_HEADER is supplied by platformio.ini per environment, e.g.
//   -D BOARD_HEADER='"boards/blackpill_f411ce.h"'
// so adding a board is a new header plus a new [env:] block, with no edit to
// any source file. See boards/_template.h.
#ifndef BOARD_HEADER
#error "BOARD_HEADER is not defined -- see build_flags in platformio.ini"
#endif
#include BOARD_HEADER

// --- feature defaults -------------------------------------------------------
// Every FEATURE_* symbol gets a 0 default AFTER the board header, so `#if
// FEATURE_X` is always legal even for a feature this board has never heard
// of. That is what lets a module's own headers guard themselves without
// every board header having to list every feature in existence.
//
// Convention: features are `#define FEATURE_X 1` / `0` and tested with a
// plain `#if`. No Marlin-style ENABLED()/DISABLED() macro machinery -- the
// defaults below buy the same "undefined is off" safety with none of the
// preprocessor gymnastics.
#ifndef FEATURE_LED
#define FEATURE_LED 0
#endif
#ifndef FEATURE_BUTTON
#define FEATURE_BUTTON 0
#endif
#ifndef FEATURE_ST7789_240X240
#define FEATURE_ST7789_240X240 0
#endif
