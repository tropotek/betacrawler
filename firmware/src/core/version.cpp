#include "core/version.h"
#include "config.h"

namespace core {

// __DATE__ / __TIME__ are captured when THIS translation unit is compiled.
// It is deliberately the only thing in the file, so it is the only thing an
// incremental build can leave stale -- and a stale build date is the entire
// reason it lives alone here rather than in a header (which would stamp a
// different date into every file that included it).
//
// Caveat, accepted: on an incremental build where version.cpp itself is not
// recompiled, the reported date is that of the last full build. A clean
// build (`pio run -t clean`) is always accurate. The alternative -- stamping
// -D BUILD_TS from an extra_script -- changes a build flag on every single
// build and so forces a full rebuild every time, which costs far more than
// this is worth.
const char* buildDate() { return __DATE__ " " __TIME__; }

const char* projectName() { return FW_PROJECT_NAME; }
const char* version()     { return FW_VERSION; }
const char* boardId()     { return BOARD_ID; }

}  // namespace core
