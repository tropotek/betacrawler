#pragma once
// Project-level firmware configuration.
//
// This is the one file to edit when forking betacrawler into a real project:
// name, version, serial speed and the capacity limits below. Which *hardware*
// exists is not decided here -- that lives in the board header, included at
// the bottom.

// --- project identity -------------------------------------------------------
#define FW_PROJECT_NAME "betacrawler"
// Shared with APP_VERSION in web-app/js/app.js -- betacrawler tracks one
// project-wide version number across firmware and app.
#define FW_VERSION      "4.2.3"

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
// blackpill_f411ce registers device, system, servo, vbat, rx, drive, motor0
// and motor1 -- 8 modules, exactly at the cap, zero headroom left. Enabling
// another module on that board needs FW_MAX_MODULES raised first:
// Registry::add() silently refuses the module that doesn't fit rather than
// overflowing, and a native test covers that path, but nothing surfaces the
// refusal to a person, so don't rely on it as a warning.
#define FW_MAX_MODULES  8
// motor0/motor1 each carry a `type` param plus three brushed-only params
// (`freq`/`invert`/`brake`), taking the param table from 32 (a bare fit) to
// 40. 48 rather than a bare fit leaves the same kind of headroom FW_MAX_TLM
// already does below.
#define FW_MAX_PARAMS   48
// The shipped Black Pill build's telemetry is dominated by rx, which alone
// publishes 16 channels plus 7 link readings; motor0 and motor1 add 2 each
// (pulse width and arm state), vbat 3, and the rest split across
// system/drive/servo. 48 rather than a bare fit leaves headroom for the next
// module or field; TlmValue is 4 bytes, so the headroom costs 32 bytes of
// static RAM in main.cpp's `static TlmValue g_tlm[FW_MAX_TLM]`.
#define FW_MAX_TLM      48

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
#ifndef FEATURE_STATUS_LED
#define FEATURE_STATUS_LED 0
#endif
#ifndef FEATURE_SERVO
#define FEATURE_SERVO 0
#endif
#ifndef FEATURE_MOTOR0
#define FEATURE_MOTOR0 0
#endif
#ifndef FEATURE_MOTOR1
#define FEATURE_MOTOR1 0
#endif
#ifndef FEATURE_DRIVE
#define FEATURE_DRIVE 0
#endif
#ifndef FEATURE_DFU
#define FEATURE_DFU 0
#endif
#ifndef FEATURE_RX
#define FEATURE_RX 0
#endif
