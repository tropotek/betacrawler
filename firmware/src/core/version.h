#pragma once

namespace core {

// Build identity, as reported by the `hello` op. Defined in src/version.cpp
// (not inline here) so the build timestamp is captured in exactly one
// translation unit -- see the comment there.
const char* projectName();   // FW_PROJECT_NAME
const char* version();       // FW_VERSION
const char* boardId();       // BOARD_ID, from the selected board header
const char* buildDate();     // "Jul 26 2026 14:03:11"

}  // namespace core
