#!/usr/bin/env python3
"""Build the firmware images the apps ship with, into app/firmware/ and
web-app/firmware/.

Run at RELEASE time, by hand. This script is the only thing that puts anything
in either destination. `app/firmware/` is gitignored build output, populated
before the desktop app is packaged into an executable. `web-app/firmware/` is
committed instead: the static site has no backend to build an image on demand,
so what it flashes has to travel with it -- re-run this and commit the result
after any firmware source change, which web-app/tests/firmware-bundle.test.js
checks via the manifest's fw_source_sha256.

    python app/tools/bundle_firmware.py                    # blackpill_f411ce
    python app/tools/bundle_firmware.py board_a board_b    # the release set
    python app/tools/bundle_firmware.py --all              # every board target
    python app/tools/bundle_firmware.py --add other_board  # merge, don't prune
    python app/tools/bundle_firmware.py --dry-run          # report, change nothing
    python app/tools/bundle_firmware.py --no-build         # bundle what's already built

What lands in the bundle is what the app is allowed to flash (app/backend/
firmware.py serves it, and re-checks the sha256 before every write), so the
whole value of this script is that the manifest cannot quietly describe a
binary that isn't there. Everything below exists to enforce that.

An esptool-method env (currently esp32_wroom32) bundles a merged single
binary built from PlatformIO's four separate output files, not a copy of
firmware.bin directly -- see merge_esp32_image().

That invariant is why a multi-board run is ALL-OR-NOTHING. Every env is built
and validated before anything is written, so a second board failing to compile
cannot leave behind a manifest that looks like a complete release and isn't.

By default the manifest describes EXACTLY the envs named in this run, and
images from a previous run are pruned. One command is one release. `--add`
merges into what is already there instead, for building boards one at a time.

Stdlib only, on purpose: this runs from a bare checkout with no venv active.
"""
import argparse
import hashlib
import json
import os
import re
import shutil
import subprocess
import sys
from pathlib import Path

# app/tools/bundle_firmware.py -> app/tools -> app -> <repo root>
# Read through module attributes rather than captured at import inside the
# functions below, so the tests can point the whole script at a fixture tree.
ROOT = Path(__file__).resolve().parents[2]
FIRMWARE = ROOT / "firmware"
# Every successful run writes each of these identically. app/firmware/ is
# gitignored build output; web-app/firmware/ is a committed release artifact
# the static site serves, since it has no backend to build one on demand.
BUNDLES = [ROOT / "app" / "firmware", ROOT / "web-app" / "firmware"]

DEFAULT_ENV = "blackpill_f411ce"

# Real path on this machine, confirmed present. If the ESP32 platform is
# reinstalled to a different location this needs updating -- there is no
# portable way to derive it without invoking PlatformIO itself, which this
# stdlib-only script deliberately avoids (see the module docstring).
BOOT_APP0_PATH = (Path.home() / ".platformio" / "packages"
                  / "framework-arduinoespressif32" / "tools" / "partitions"
                  / "boot_app0.bin")

# Verified against esp32dev.json (flash_size/flash_mode/f_flash) and a real
# `esptool merge-bin` run against this project's own esp32_wroom32 build.
ESP32_MERGE_LAYOUT = (
    ("0x1000", "bootloader.bin"),
    ("0x8000", "partitions.bin"),
    ("0xe000", None),          # boot_app0.bin, resolved via BOOT_APP0_PATH
    ("0x10000", "firmware.bin"),
)


def manifest_path(bundle: Path) -> Path:
    return bundle / "manifest.json"


def bin_path_for(env: str) -> Path:
    return FIRMWARE / ".pio" / "build" / env / "firmware.bin"

# The firmware stamps __DATE__ " " __TIME__ into exactly one translation unit
# (firmware/src/core/version.cpp) and reports it over the wire in `hello`.
# Pulling that same string back out of the binary is what lets the app compare
# a running device against a bundled image byte-for-byte rather than trusting
# two version numbers to have been bumped together.
BUILD_STAMP = re.compile(
    rb"(?:Jan|Feb|Mar|Apr|May|Jun|Jul|Aug|Sep|Oct|Nov|Dec) "
    rb"[ 0-9][0-9] [0-9]{4} [0-9]{2}:[0-9]{2}:[0-9]{2}"
)


class BundleError(Exception):
    """Anything that should stop the release with a readable message."""


# --- reading the firmware's own source of truth --------------------------------
# Every value in the manifest is derived, never typed in. A hand-maintained
# manifest drifts from the binary it describes, and the drift is invisible
# until someone flashes it.

def read_define(text: str, name: str) -> str | None:
    """Value of `#define NAME ...`, with surrounding quotes stripped."""
    m = re.search(rf'^\s*#\s*define\s+{re.escape(name)}\s+(.+?)\s*$', text, re.M)
    if not m:
        return None
    return m.group(1).strip().strip('"')


def _env_block(env: str) -> str:
    """The [env:<env>] section's body from platformio.ini, or raise."""
    ini = (FIRMWARE / "platformio.ini").read_text()
    block = re.search(rf'^\[env:{re.escape(env)}\](.*?)(?=^\[|\Z)', ini, re.M | re.S)
    if not block:
        raise BundleError(f"no [env:{env}] section in firmware/platformio.ini")
    return block.group(1)


def board_header_path(env: str) -> Path:
    """Resolve the board header this env compiles against, from platformio.ini.

    Parsed rather than assumed: the whole point of the board-header design is
    that `-D BOARD_HEADER` is the single place the mapping lives, so guessing
    "<env>.h" here would reintroduce exactly the drift that indirection
    removes.
    """
    block = _env_block(env)
    m = re.search(r"-D\s+BOARD_HEADER\s*=\s*'\"(.+?)\"'", block)
    if not m:
        raise BundleError(f"[env:{env}] defines no BOARD_HEADER")
    header = FIRMWARE / "include" / m.group(1)
    if not header.is_file():
        raise BundleError(f"board header {header} does not exist")
    return header


def method_for(env: str) -> str:
    """Method for an env: "esptool" if ESP32, else "dfu".

    Read from the same platformio.ini block board_header_path() already
    parses, keyed on the FW_MCU_ESP32 build flag the 2026-08-01 ESP32 design
    introduced to guard architecture-specific driver bodies -- reusing it
    here means there is exactly one place in the tree that says "this env is
    an ESP32", not two that could drift apart.
    """
    block = _env_block(env)
    if re.search(r"-D\s+FW_MCU_ESP32\s*=\s*1\b", block):
        return "esptool"
    return "dfu"


def all_board_envs() -> list[str]:
    """Every [env:*] in platformio.ini that is a real board target, in file
    order. Excluded without a name-based blocklist, which would itself go
    stale -- exactly the kind of drift this script exists to prevent (see
    module docstring). Two structural signals, both required:

    - `-D BOARD_HEADER=...` -- what board_header_path()/method_for() key off.
    - NOT `platform = native` -- [env:native] (the host-side Unity suite)
      deliberately DOES set BOARD_HEADER too (the real board header, per the
      comment on that section: its fixtures must describe the actual
      device's parameter/telemetry set), so BOARD_HEADER alone is not enough
      to tell it apart from a shippable board env. `platform` is PlatformIO's
      own field for which toolchain an env builds with -- board envs are
      always `ststm32`/`espressif32`, never `native` -- so this is the same
      kind of structural signal as BOARD_HEADER, not a hardcoded env name.
    """
    ini = (FIRMWARE / "platformio.ini").read_text()
    envs = []
    for m in re.finditer(r'^\[env:([\w-]+)\]', ini, re.M):
        env = m.group(1)
        # _env_block() raises if the env has no section at all, but every
        # name here just came from a regex match against this same file, so
        # that can't happen in practice.
        block = _env_block(env)
        has_board_header = re.search(r"-D\s+BOARD_HEADER\s*=", block)
        is_native = re.search(r"^platform\s*=\s*native\s*$", block, re.M)
        if has_board_header and not is_native:
            envs.append(env)
    return envs


def enabled_features(header_text: str) -> list[str]:
    """FEATURE_* flags set to 1, lowercased and stripped of the prefix.

    Deliberately not mapped to prettier names: a lookup table here would need
    an edit every time a module is added, which is the drift this tool exists
    to prevent. "led, button, servo" is honest and self-maintaining.
    """
    found = re.findall(r'^\s*#\s*define\s+FEATURE_(\w+)\s+1\s*$', header_text, re.M)
    return [f.lower() for f in found]


def proto_version() -> int:
    text = (FIRMWARE / "src" / "core" / "types.h").read_text()
    m = re.search(r'kProtoVersion\s*=\s*(\d+)', text)
    if not m:
        raise BundleError("could not find kProtoVersion in firmware/src/core/types.h")
    return int(m.group(1))


def app_version() -> str:
    text = (ROOT / "web-app" / "js" / "app.js").read_text()
    m = re.search(r"^const APP_VERSION\s*=\s*'([^']+)'", text, re.M)
    if not m:
        raise BundleError("could not find APP_VERSION in web-app/js/app.js")
    return m.group(1)


# What every bundled image is built from. firmware/test/ and firmware/scripts/
# are excluded: neither reaches the binary.
FW_SOURCE_DIRS = ("include", "src")
FW_SOURCE_FILES = ("platformio.ini",)


def fw_source_sha256() -> str:
    """Fingerprint of the firmware sources, path-sensitive.

    Recorded in the manifest so a committed bundle can be checked against the
    tree it was built from. Each file's relative path is hashed along with its
    contents, so adding, renaming or deleting one counts as a change even when
    no existing file was touched.
    """
    paths = [p for name in FW_SOURCE_DIRS
             for p in (FIRMWARE / name).rglob("*") if p.is_file()]
    paths += [FIRMWARE / n for n in FW_SOURCE_FILES if (FIRMWARE / n).is_file()]
    digest = hashlib.sha256()
    for path in sorted(paths, key=lambda p: p.relative_to(FIRMWARE).as_posix()):
        digest.update(path.relative_to(FIRMWARE).as_posix().encode())
        digest.update(path.read_bytes())
    return digest.hexdigest()


# --- checking the binary actually matches those sources ------------------------

def embedded_build_date(blob: bytes) -> str:
    matches = BUILD_STAMP.findall(blob)
    if not matches:
        raise BundleError(
            "no build timestamp found in the binary -- is this really a "
            "firmware image built from this project?")
    # More than one would mean something else in the image carries a
    # __DATE__ too, and we'd have no way to tell which is the firmware's.
    if len({m for m in matches}) > 1:
        raise BundleError(
            f"ambiguous build timestamps in the binary: "
            f"{[m.decode() for m in matches]}")
    return matches[0].decode()


def check_identity(blob: bytes, name: str, version: str, board: str) -> None:
    """The binary must contain the identity the headers currently claim.

    This is the strong half of the staleness check. A timestamp comparison
    only catches a binary that is *older* than the sources; this catches one
    built from *different* sources -- a different board header, a version
    bumped after the last build -- which is the failure that actually ships.
    """
    for label, value in (("FW_PROJECT_NAME", name),
                         ("FW_VERSION", version),
                         ("BOARD_ID", board)):
        if value.encode() not in blob:
            raise BundleError(
                f"binary does not contain {label} = {value!r} as the headers "
                f"claim -- it was built from different sources. Rebuild:\n"
                f"    ~/.platformio/penv/bin/pio run -e <env>")


def find_pio() -> str | None:
    """Locate the PlatformIO CLI. It is deliberately NOT on PATH on this
    machine (see CLAUDE.md), so the venv location is tried first."""
    candidate = Path.home() / ".platformio" / "penv" / "bin" / "pio"
    if candidate.is_file() and os.access(candidate, os.X_OK):
        return str(candidate)
    return shutil.which("pio")


def find_esptool() -> str:
    """Locate the esptool CLI, the same way find_pio() locates PlatformIO.

    esptool is a pip dependency of the APP (app/requirements.txt), installed
    into app/.venv/ -- while this script's documented invocation is
    `python3 app/tools/bundle_firmware.py`, i.e. the SYSTEM python3, whose
    PATH has neither the package nor the console script. Trying the venv copy
    first makes PATH irrelevant for the normal case; the PATH fallback covers
    a system-wide install.

    Raises rather than returning None (unlike find_pio(), whose caller has its
    own message for that) so a missing tool reads as an instruction instead of
    a FileNotFoundError traceback out of subprocess.
    """
    candidate = ROOT / "app" / ".venv" / "bin" / "esptool"
    if candidate.is_file() and os.access(candidate, os.X_OK):
        return str(candidate)
    found = shutil.which("esptool")
    if found:
        return found
    raise BundleError(
        "esptool not found. It ships as a dependency of the app's venv:\n"
        "    app/.venv/bin/pip install -r app/requirements.txt\n"
        "(or install it so `esptool` is on PATH). Only esptool-method envs "
        "-- the ESP32 targets -- need it.")


def force_version_rebuild(env: str) -> list[Path]:
    """Delete version.cpp's object file so the build re-stamps __DATE__/__TIME__.

    The build stamp is the app's ONLY way to tell a running board apart from a
    bundled image (isRunning() in app.js compares `built` and `version`, and
    FW_VERSION stays 1.0.0 by project policy). But __DATE__/__TIME__ are frozen
    into version.cpp's object file at whatever moment it last compiled, and
    SCons will not recompile a translation unit whose inputs are unchanged.

    Editing anything SCons cannot see as an input to version.cpp -- which is
    everything except include/**/*.h, whose hash config_hash.py folds into
    -D FW_CONFIG_HASH -- therefore produces a new firmware.bin carrying a stale
    stamp. That is not hypothetical: the `revert` op lives in src/core/, so the
    image that first contained it still claimed the previous build's timestamp,
    and the Firmware page marked it "currently running" on a board that had
    never seen it.

    Fixed here rather than by widening config_hash.py to src/**/*.h, because
    the stamp only has to be truthful for images that SHIP. Hashing source
    headers would make every header edit a full rebuild of every translation
    unit during ordinary development, to fix a problem that only matters at
    release time. One object file, rebuilt once per release, buys the same
    guarantee.

    Returns the paths actually removed. Missing files are not an error: a clean
    checkout has no build directory at all, and that is the normal first run.
    """
    build_dir = FIRMWARE / ".pio" / "build" / env
    removed = []
    if not build_dir.is_dir():
        return removed
    # Globbed rather than hardcoded to src/core/version.cpp.o: the layout under
    # .pio/build is PlatformIO's, not ours, and a miss here would fail silently
    # by reintroducing exactly the stale stamp this exists to prevent.
    for obj in sorted(build_dir.rglob("version.cpp.o")):
        obj.unlink()
        removed.append(obj)
    return removed


def run_build(env: str, pio: str) -> None:
    """Build the env, so the bundled artifact provably matches this tree.

    Building here rather than checking timestamps is a correctness fix, not a
    convenience. SCons decides what to rebuild from CONTENT signatures, so a
    `touch`, a `git checkout`, or an editor that rewrites a file byte-identical
    all leave sources newer than firmware.bin while the binary is perfectly
    current -- and no amount of rebuilding clears the skew, because there is
    nothing to rebuild. An mtime check in that state fires forever and trains
    you to pass --force, which is worse than having no check at all.
    """
    for obj in force_version_rebuild(env):
        print(f"  re-stamping {obj.relative_to(FIRMWARE)}")
    print(f"building {env} ...")
    proc = subprocess.run([pio, "run", "-e", env], cwd=FIRMWARE,
                          capture_output=True, text=True)
    if proc.returncode != 0:
        tail = "\n".join((proc.stdout + proc.stderr).strip().splitlines()[-15:])
        raise BundleError(f"`pio run -e {env}` failed:\n{tail}")


def _run_esptool(argv: list[str]) -> int:
    try:
        proc = subprocess.run(argv, capture_output=True, text=True)
    except OSError as exc:
        # run_build() reports a broken `pio` as a BundleError rather than a
        # traceback; the same applies here, for the same reason: the caller
        # prints BundleError as the tool's error message.
        raise BundleError(f"could not run esptool ({argv[0]}): {exc}") from exc
    if proc.returncode != 0:
        raise BundleError(
            "esptool merge-bin failed:\n" +
            (proc.stdout + proc.stderr).strip())
    return proc.returncode


def merge_esp32_image(env: str, esptool: str | None = None, runner=None) -> Path:
    """Fold this env's four PlatformIO build outputs into one flashable file.

    `runner` is injected the same way run_build()'s `builder` param is --
    tests replace it with something that writes a fake merged file instead
    of shelling out to a real esptool. Resolved to _run_esptool INSIDE the
    body (a name lookup at call time), not as the default parameter value --
    a default of `runner=_run_esptool` would bind the function object at
    def-time, so a test's `monkeypatch.setattr(mod, "_run_esptool", fake)`
    would silently have no effect on any caller (like plan_entry() below)
    that doesn't pass its own runner explicitly.

    `esptool` is resolved the same way and for the same reason -- a bare
    "esptool" only works when the app venv happens to be on PATH, which it
    is not under this script's documented invocation. See find_esptool().
    """
    runner = runner or _run_esptool
    build_dir = FIRMWARE / ".pio" / "build" / env
    inputs = []
    for offset, name in ESP32_MERGE_LAYOUT:
        path = build_dir / name if name else BOOT_APP0_PATH
        if not path.is_file():
            raise BundleError(
                f"{path} does not exist -- build {env} first, or (for "
                f"boot_app0.bin) check the ESP32 Arduino platform is "
                f"installed")
        inputs.append((offset, path))

    merged = build_dir / "merged-flash.bin"
    argv = [esptool or find_esptool(), "--chip", "esp32", "merge-bin", "-o", str(merged),
            "--flash-mode", "dio", "--flash-freq", "40m", "--flash-size", "4MB"]
    for offset, path in inputs:
        argv += [offset, str(path)]

    runner(argv)
    if not merged.is_file():
        raise BundleError(f"esptool merge-bin did not produce {merged}")
    return merged


def sources_newer_than(bin_path: Path) -> list[Path]:
    """Source files newer than the binary (empty means it looks current).

    Only consulted under --no-build, where it is the sole available signal.
    See run_build() for why it is not trustworthy on its own.
    """
    stamp = bin_path.stat().st_mtime
    newer = []
    for base in (FIRMWARE / "src", FIRMWARE / "include"):
        for f in base.rglob("*"):
            if f.is_file() and f.stat().st_mtime > stamp:
                newer.append(f.relative_to(ROOT))
    return sorted(newer)


# --- vector table --------------------------------------------------------------
# Same check app/backend/firmware.py applies to an uploaded file. Running it
# here too means a bundled image can never be the thing that trips it at flash
# time, when the board is already in DFU and the user is committed.
SRAM_LO, SRAM_HI = 0x2000_0000, 0x2002_0000   # HI is INCLUSIVE: a real
# betacrawler build has MSP exactly 0x20020000, the top of the F411's 128KB SRAM.
FLASH_LO, FLASH_HI = 0x0800_0000, 0x0808_0000


def check_vector_table(blob: bytes) -> None:
    if len(blob) < 8:
        raise BundleError("binary is too small to contain a vector table")
    msp = int.from_bytes(blob[0:4], "little")
    reset = int.from_bytes(blob[4:8], "little")
    if not (SRAM_LO <= msp <= SRAM_HI):
        raise BundleError(
            f"initial stack pointer 0x{msp:08x} is not in SRAM "
            f"(0x{SRAM_LO:08x}..0x{SRAM_HI:08x}) -- not a raw .bin?")
    if not (FLASH_LO <= reset < FLASH_HI) or not reset & 1:
        raise BundleError(
            f"reset vector 0x{reset:08x} is not a Thumb address in flash "
            f"(0x{FLASH_LO:08x}..0x{FLASH_HI:08x})")


# A merged ESP32 image is sparse: bytes 0x0-0xFFF are 0xFF padding because
# bootloader.bin (the first real content) starts at offset 0x1000, not 0.
# Verified against a real `esptool merge-bin` run -- checking blob[0] would
# reject every genuine merged image AND wrongly accept a bare, unbootable
# firmware.bin (which does have the magic byte at offset 0 on its own).
ESP32_IMAGE_MAGIC_OFFSET = 0x1000
ESP32_IMAGE_MAGIC = 0xE9


def check_esp32_image(blob: bytes) -> None:
    if len(blob) < ESP32_IMAGE_MAGIC_OFFSET + 1:
        raise BundleError(
            f"binary is only {len(blob)} bytes -- too small to contain a "
            f"bootloader image at offset 0x{ESP32_IMAGE_MAGIC_OFFSET:x}")
    if blob[ESP32_IMAGE_MAGIC_OFFSET] != ESP32_IMAGE_MAGIC:
        raise BundleError(
            f"byte at offset 0x{ESP32_IMAGE_MAGIC_OFFSET:x} is "
            f"0x{blob[ESP32_IMAGE_MAGIC_OFFSET]:02x}, not the ESP image magic "
            f"(0x{ESP32_IMAGE_MAGIC:02x}) -- not a raw merged esptool image?")


# --- manifest ------------------------------------------------------------------

def load_manifest(bundle: Path) -> dict:
    path = manifest_path(bundle)
    if not path.is_file():
        return {"app_version": None, "images": []}
    try:
        data = json.loads(path.read_text())
    except json.JSONDecodeError as exc:
        raise BundleError(f"{path} is not valid JSON: {exc}") from exc
    if not isinstance(data.get("images"), list):
        raise BundleError(f"{path} has no `images` list")
    return data


def write_manifest(bundle: Path, data: dict) -> None:
    path = manifest_path(bundle)
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(data, indent=2) + "\n")


def prune(bundle: Path, old: dict, new: dict) -> list[Path]:
    """Delete images the previous manifest listed and the new one doesn't.

    Scoped to what a manifest named, never a wildcard sweep of the directory:
    `rm -rf <bundle>/*` is shorter and would also throw away a binary
    someone had dropped in there by hand. Since this is the one path in the
    script that deletes anything, entries are also checked to actually live
    under the bundle before being unlinked -- a `file` of "../../secrets"
    should not be reachable even from a manifest we wrote ourselves.
    """
    keep = {img.get("file") for img in new.get("images", [])}
    root = bundle.resolve()
    removed, parents = [], set()

    for img in old.get("images", []):
        rel = img.get("file")
        if not rel or rel in keep:
            continue
        path = (bundle / rel).resolve()
        if not path.is_relative_to(root):
            continue
        parents.add(path.parent)
        if path.is_file():
            path.unlink()
            removed.append(path)

    # A board directory left empty by the above is noise; a board that still
    # has images keeps its directory untouched.
    for d in parents:
        if d != root and d.is_dir() and not any(d.iterdir()):
            d.rmdir()
    return removed


def plan_entry(env: str, force: bool = False, build: bool = True,
               pio: str | None = None, builder=None) -> dict:
    """Build and validate one env, and return its manifest entry.

    Deliberately writes NOTHING under app/firmware/. Separating "work out what
    this env would contribute" from "write it" is what lets a multi-board run
    fail cleanly: every env goes through here first, so the release either
    lands whole or not at all.

    `builder` is injected the same way `SerialLink` takes `open_port` and
    `DfuFlasher` takes `runner` -- it is the one call that needs a toolchain,
    so the tests replace it and exercise everything else for real. Resolved
    to run_build INSIDE the body, exactly as merge_esp32_image() resolves its
    `runner`: a `builder=run_build` default would bind the function object at
    def-time, so a `monkeypatch.setattr(mod, "run_build", fake)` would be
    silently ignored by any caller that omits `builder=`.
    """
    builder = builder or run_build
    method = method_for(env)
    bin_path = bin_path_for(env)

    if build:
        builder(env, pio)

    if not bin_path.is_file():
        raise BundleError(
            f"{bin_path.relative_to(ROOT)} does not exist. Build it first:\n"
            f"    ~/.platformio/penv/bin/pio run -e {env}")

    config = (FIRMWARE / "include" / "config.h").read_text()
    header = board_header_path(env)
    header_text = header.read_text()

    name = read_define(config, "FW_PROJECT_NAME")
    version = read_define(config, "FW_VERSION")
    board = read_define(header_text, "BOARD_ID")
    if not (name and version and board):
        raise BundleError(
            "could not read FW_PROJECT_NAME/FW_VERSION from config.h or "
            f"BOARD_ID from {header.relative_to(ROOT)}")

    # Format check BEFORE check_identity(), same order the pre-existing code
    # already used for the DFU path (check_vector_table() then
    # check_identity()) -- test_a_binary_that_fails_validation_stops_the_
    # whole_release feeds an ELF fixture and asserts the error mentions
    # "stack pointer", which only holds if the vector-table check still runs
    # first. Swapping the order would instead report a missing-identity
    # error for the same fixture, silently changing what that test proves.
    fw_blob = bin_path.read_bytes()

    if method == "esptool":
        image_path = merge_esp32_image(env)
        blob = image_path.read_bytes()
        check_esp32_image(blob)
    else:
        image_path = bin_path
        blob = fw_blob
        check_vector_table(blob)

    # check_identity()/embedded_build_date() scan firmware.bin -- the
    # ESP32 app partition -- for the FW_PROJECT_NAME/FW_VERSION/BOARD_ID
    # strings and the __DATE__ stamp, regardless of whether that's the file
    # actually shipped.
    check_identity(fw_blob, name, version, board)

    # Only meaningful under --no-build: with a build just run, the binary is
    # current by construction and a timestamp comparison can only mislead.
    if not build and not force:
        stale = sources_newer_than(bin_path)
        if stale:
            listed = "\n".join(f"      {p}" for p in stale[:10])
            more = f"\n      ... and {len(stale) - 10} more" if len(stale) > 10 else ""
            raise BundleError(
                f"{len(stale)} source file(s) are newer than the binary:\n{listed}{more}\n"
                f"    Drop --no-build to rebuild and bundle in one step.\n"
                f"    (If the binary really is current -- a touch or a checkout can\n"
                f"     skew mtimes without changing content -- pass --force.)")

    entry = {
        "id": f"{board}-{name}-{version}",
        "board": board,
        "name": name,
        "version": version,
        "built": embedded_build_date(fw_blob),
        "proto": proto_version(),
        "method": method,
        "file": f"{board}/{name}-{version}.bin",
        "size": len(blob),
        "sha256": hashlib.sha256(blob).hexdigest(),
        "notes": ", ".join(enabled_features(header_text)) or "no optional modules",
        "_source": image_path,   # internal only, consumed by release() below
    }
    return entry


def release(envs: list[str], dry_run: bool = False, force: bool = False,
            build: bool = True, pio: str | None = None, add: bool = False,
            builder=None) -> tuple[list[dict], list[Path]]:
    """Build every env, then write the bundle. Returns (entries, pruned).

    Nothing under app/firmware/ is touched until all of `envs` have built and
    validated -- see the module docstring for why that ordering is the point
    of this function rather than an implementation detail.

    `builder` resolves to run_build inside the body, not as a default -- see
    plan_entry() for why that distinction is not cosmetic.
    """
    builder = builder or run_build
    envs = list(envs) or [DEFAULT_ENV]
    dupes = sorted({e for e in envs if envs.count(e) > 1})
    if dupes:
        raise BundleError(f"env named more than once: {', '.join(dupes)}")

    tool = pio
    if build and tool is None:
        tool = find_pio()
        if tool is None:
            raise BundleError(
                "could not find the PlatformIO CLI (tried "
                "~/.platformio/penv/bin/pio and PATH). Pass --pio <path>, or "
                "--no-build to bundle an already-built binary.")

    # Read before building rather than just before writing: a corrupt manifest
    # is a one-second failure, and finding it after a multi-board build has
    # already run is a needlessly expensive way to be told.
    olds = {bundle: load_manifest(bundle) for bundle in BUNDLES}
    primary = olds[BUNDLES[0]]

    plans = [(env, plan_entry(env, force=force, build=build, pio=tool,
                              builder=builder))
             for env in envs]
    entries = [entry for _, entry in plans]

    # Two envs compiling the same board header would silently overwrite each
    # other's image, and the manifest would report whichever won. Catch it
    # here, while nothing has been written.
    ids = [e["id"] for e in entries]
    clash = sorted({i for i in ids if ids.count(i) > 1})
    if clash:
        raise BundleError(
            f"these envs produce the same image id: {', '.join(clash)} -- "
            f"they build the same board, name and version")

    if dry_run:
        # Stripped here too: dry_run output is returned to the caller
        # (potentially printed), and _source is an internal Path object that
        # was never meant to leak past this function.
        for _, entry in plans:
            entry.pop("_source", None)
        return entries, []

    # Without --add the run IS the release, so anything the previous manifest
    # listed and this one doesn't is gone. With it, prior entries survive and
    # only same-id ones are replaced.
    fresh = set(ids)
    kept = [img for img in primary["images"] if img.get("id") not in fresh] if add else []
    # One manifest, computed once and written to every destination verbatim.
    # Stable order, so re-releasing produces a diff of only the fields that
    # actually changed.
    data = {"app_version": app_version(),
            "fw_source_sha256": fw_source_sha256(),
            "images": sorted(kept + entries, key=lambda i: i["id"])}

    for bundle in BUNDLES:
        for env, entry in plans:
            dest = bundle / entry["file"]
            dest.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(entry["_source"], dest)

    # _source is an internal Path object, not JSON-serializable and not
    # something the manifest should carry -- stripped only now, after the
    # copy loop above has used it.
    for _, entry in plans:
        entry.pop("_source", None)

    # After the copies, so an image this run rewrote in place is never a
    # deletion candidate. Each destination is pruned against its OWN previous
    # manifest, so a stale image in one is removed even when the other never
    # had it.
    pruned = []
    for bundle in BUNDLES:
        if not add:
            pruned += prune(bundle, olds[bundle], data)
        write_manifest(bundle, data)
    return entries, pruned


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("envs", nargs="*", metavar="ENV", default=[],
                    help=f"PlatformIO envs to build and ship "
                         f"(default: {DEFAULT_ENV})")
    ap.add_argument("--all", action="store_true",
                    help="build every board env with a BOARD_HEADER, "
                         "instead of just the default")
    ap.add_argument("--add", action="store_true",
                    help="merge into the existing manifest instead of "
                         "replacing it with exactly these envs")
    ap.add_argument("--dry-run", action="store_true",
                    help="report what would be bundled, write nothing")
    ap.add_argument("--no-build", action="store_true",
                    help="bundle the existing binaries instead of rebuilding first")
    ap.add_argument("--pio", help="path to the PlatformIO CLI")
    ap.add_argument("--force", action="store_true",
                    help="with --no-build, bundle even if sources look newer")
    args = ap.parse_args()

    # A plain ap.error() check rather than add_mutually_exclusive_group():
    # argparse's mutually-exclusive groups are unreliable for a positional
    # nargs="*" arg (zero-given doesn't consistently register as "used"
    # across versions), so this is deterministic where that would not be.
    if args.all and args.envs:
        ap.error("--all cannot be combined with explicit ENV arguments")

    envs = all_board_envs() if args.all else args.envs

    try:
        entries, pruned = release(envs, dry_run=args.dry_run,
                                  force=args.force, build=not args.no_build,
                                  pio=args.pio, add=args.add)
    except BundleError as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 1

    verb = "would bundle" if args.dry_run else "bundled"
    for entry in entries:
        print(f"{verb} {entry['id']}")
        print(f"  board   {entry['board']}")
        print(f"  built   {entry['built']}")
        print(f"  proto   {entry['proto']}")
        print(f"  modules {entry['notes']}")
        print(f"  size    {entry['size']} bytes")
        print(f"  sha256  {entry['sha256']}")
        if not args.dry_run:
            for bundle in BUNDLES:
                print(f"  -> {bundle.relative_to(ROOT)}/{entry['file']}")
    for path in pruned:
        print(f"removed  {path.relative_to(ROOT)}")
    if not args.dry_run:
        for bundle in BUNDLES:
            print(f"{len(entries)} image(s) -> {bundle.relative_to(ROOT)}/manifest.json")
    return 0


if __name__ == "__main__":
    sys.exit(main())
