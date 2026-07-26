"""Makes edits to include/ actually trigger a rebuild.

config.h reaches the board header through `#include BOARD_HEADER`, where
BOARD_HEADER is a -D build flag. SCons' C scanner cannot expand that macro, so
the board header never enters the dependency graph -- flipping FEATURE_LED and
rebuilding silently reuses the previous object files and produces a binary that
does not match the config. That is a nasty failure mode for a template whose
whole point is compile-time configuration, and it is invisible: the build
succeeds and reports the wrong sizes.

The fix is to fold a hash of every configuration header into a preprocessor
define. When any header under include/ changes, the flag changes, the compile
command changes, and SCons rebuilds. The define itself is never referenced by
any code -- carrying the signature is its entire job.

Cost: editing a config header forces a full rebuild (correct, and rare).
Editing anything else costs nothing.
"""
Import("env")  # noqa: F821  -- injected by SCons

import hashlib
import pathlib

include_dir = pathlib.Path(env.subst("$PROJECT_DIR")) / "include"  # noqa: F821

digest = hashlib.sha256()
for path in sorted(include_dir.rglob("*.h")):
    # Hash the name as well as the contents, so adding or renaming a board
    # header counts as a change even if no existing file was touched.
    digest.update(path.relative_to(include_dir).as_posix().encode())
    digest.update(path.read_bytes())

env.Append(CPPDEFINES=[("FW_CONFIG_HASH", "0x" + digest.hexdigest()[:8] + "u")])  # noqa: F821
