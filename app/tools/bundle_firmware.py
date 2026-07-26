#!/usr/bin/env python3
"""Copy a built firmware image into the app's shipped bundle.

Run at RELEASE time, not on every build -- deliberately. A PlatformIO
post-build hook would rewrite a committed binary on every `pio run`, so every
dev build would show up as a dirty file and the history would fill with noise
that means nothing. This is an explicit act instead.

    python app/tools/bundle_firmware.py                    # blackpill_f411ce
    python app/tools/bundle_firmware.py --env other_board
    python app/tools/bundle_firmware.py --dry-run          # report, change nothing
    python app/tools/bundle_firmware.py --no-build         # bundle what's already built

What lands in the bundle is what the app is allowed to flash (app/backend/
firmware.py serves it, and re-checks the sha256 before every write), so the
whole value of this script is that the manifest cannot quietly describe a
binary that isn't there. Everything below exists to enforce that.

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
ROOT = Path(__file__).resolve().parents[2]
FIRMWARE = ROOT / "firmware"
BUNDLE = ROOT / "app" / "firmware"
MANIFEST = BUNDLE / "manifest.json"

DEFAULT_ENV = "blackpill_f411ce"

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


def board_header_path(env: str) -> Path:
    """Resolve the board header this env compiles against, from platformio.ini.

    Parsed rather than assumed: the whole point of the board-header design is
    that `-D BOARD_HEADER` is the single place the mapping lives, so guessing
    "<env>.h" here would reintroduce exactly the drift that indirection
    removes.
    """
    ini = (FIRMWARE / "platformio.ini").read_text()
    block = re.search(rf'^\[env:{re.escape(env)}\](.*?)(?=^\[|\Z)', ini, re.M | re.S)
    if not block:
        raise BundleError(f"no [env:{env}] section in firmware/platformio.ini")
    m = re.search(r"-D\s+BOARD_HEADER\s*=\s*'\"(.+?)\"'", block.group(1))
    if not m:
        raise BundleError(f"[env:{env}] defines no BOARD_HEADER")
    header = FIRMWARE / "include" / m.group(1)
    if not header.is_file():
        raise BundleError(f"board header {header} does not exist")
    return header


def enabled_features(header_text: str) -> list[str]:
    """FEATURE_* flags set to 1, lowercased and stripped of the prefix.

    Deliberately not mapped to prettier names: a lookup table here would need
    an edit every time a module is added, which is the drift this tool exists
    to prevent. "led, button, st7789_240x240" is honest and self-maintaining.
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
    text = (ROOT / "app" / "web" / "app.js").read_text()
    m = re.search(r"^const APP_VERSION\s*=\s*'([^']+)'", text, re.M)
    if not m:
        raise BundleError("could not find APP_VERSION in app/web/app.js")
    return m.group(1)


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
    print(f"building {env} ...")
    proc = subprocess.run([pio, "run", "-e", env], cwd=FIRMWARE,
                          capture_output=True, text=True)
    if proc.returncode != 0:
        tail = "\n".join((proc.stdout + proc.stderr).strip().splitlines()[-15:])
        raise BundleError(f"`pio run -e {env}` failed:\n{tail}")


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
# silkscreen build has MSP exactly 0x20020000, the top of the F411's 128KB SRAM.
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


# --- manifest ------------------------------------------------------------------

def load_manifest() -> dict:
    if not MANIFEST.is_file():
        return {"app_version": app_version(), "images": []}
    try:
        return json.loads(MANIFEST.read_text())
    except json.JSONDecodeError as exc:
        raise BundleError(f"{MANIFEST} is not valid JSON: {exc}") from exc


def write_manifest(data: dict) -> None:
    MANIFEST.parent.mkdir(parents=True, exist_ok=True)
    MANIFEST.write_text(json.dumps(data, indent=2) + "\n")


def bundle(env: str, dry_run: bool = False, force: bool = False,
           build: bool = True, pio: str | None = None) -> dict:
    bin_path = FIRMWARE / ".pio" / "build" / env / "firmware.bin"

    if build:
        tool = pio or find_pio()
        if tool is None:
            raise BundleError(
                "could not find the PlatformIO CLI (tried "
                "~/.platformio/penv/bin/pio and PATH). Pass --pio <path>, or "
                "--no-build to bundle an already-built binary.")
        run_build(env, tool)

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

    blob = bin_path.read_bytes()
    check_vector_table(blob)
    check_identity(blob, name, version, board)

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
        "built": embedded_build_date(blob),
        "proto": proto_version(),
        "method": "dfu",
        "file": f"{board}/{name}-{version}.bin",
        "size": len(blob),
        "sha256": hashlib.sha256(blob).hexdigest(),
        "notes": ", ".join(enabled_features(header_text)) or "no optional modules",
    }

    if dry_run:
        return entry

    dest = BUNDLE / entry["file"]
    dest.parent.mkdir(parents=True, exist_ok=True)
    shutil.copyfile(bin_path, dest)

    data = load_manifest()
    data["app_version"] = app_version()
    images = [img for img in data.get("images", []) if img.get("id") != entry["id"]]
    images.append(entry)
    # Stable order, so re-bundling an existing image produces a diff of only
    # the fields that actually changed.
    data["images"] = sorted(images, key=lambda i: i["id"])
    write_manifest(data)
    return entry


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--env", default=DEFAULT_ENV,
                    help=f"PlatformIO env to bundle (default: {DEFAULT_ENV})")
    ap.add_argument("--dry-run", action="store_true",
                    help="report what would be bundled, write nothing")
    ap.add_argument("--no-build", action="store_true",
                    help="bundle the existing binary instead of rebuilding first")
    ap.add_argument("--pio", help="path to the PlatformIO CLI")
    ap.add_argument("--force", action="store_true",
                    help="with --no-build, bundle even if sources look newer")
    args = ap.parse_args()

    try:
        entry = bundle(args.env, dry_run=args.dry_run, force=args.force,
                       build=not args.no_build, pio=args.pio)
    except BundleError as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 1

    verb = "would bundle" if args.dry_run else "bundled"
    print(f"{verb} {entry['id']}")
    print(f"  board   {entry['board']}")
    print(f"  built   {entry['built']}")
    print(f"  proto   {entry['proto']}")
    print(f"  modules {entry['notes']}")
    print(f"  size    {entry['size']} bytes")
    print(f"  sha256  {entry['sha256']}")
    if not args.dry_run:
        print(f"  -> app/firmware/{entry['file']}")
        print(f"  -> app/firmware/manifest.json")
    return 0


if __name__ == "__main__":
    sys.exit(main())
