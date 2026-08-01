"""Firmware bundle catalog and the `dfu-util` flashing path.

Two things live here, kept apart on purpose:

  * `Catalog` -- what the app shipped with. Pure filesystem + JSON.
  * `DfuFlasher` / `FlashSession` -- getting one of those images onto a board.

Everything that touches the outside world arrives through an injected
`runner`, the same way `SerialLink` takes an `open_port` callable, so the whole
module tests with no board attached and no `dfu-util` installed. That is not a
testing nicety here: the one thing this code does is overwrite a device's
flash, and the failure modes worth covering (a truncated download, a board
unplugged mid-write, a mismatched image) are all ones you cannot stage on real
hardware on demand.

The device half of DFU entry is the firmware's `dfu` op (see
`DeviceModel.enter_dfu`). This module never asks a board to do anything -- by
the time it runs, the board is already in DFU mode, however it got there.
"""
import hashlib
import json
import logging
import os
import re
import subprocess
import threading
import time
from pathlib import Path

log = logging.getLogger(__name__)

BUNDLE_DIR = Path(__file__).resolve().parent.parent / "firmware"

# The STM32 ROM bootloader's USB identity. Every STM32F4 in DFU mode presents
# exactly this -- which is why nothing here can tell one board from another,
# and why the UI has to carry the board identity forward from the last `hello`
# instead of asking the device. See _notes/spec-dfu-upload.md.
DFU_VID, DFU_PID = 0x0483, 0xDF11
FLASH_ORIGIN = 0x0800_0000

# Bounds for the sanity check on an uploaded image. SRAM_HI is INCLUSIVE: a
# real silkscreen build has its initial stack pointer at exactly 0x20020000, the
# top of the F411's 128KB SRAM, so an exclusive bound rejects every genuine
# image. (Measured, not assumed -- see the spec.)
SRAM_LO, SRAM_HI = 0x2000_0000, 0x2002_0000
FLASH_LO, FLASH_HI = 0x0800_0000, 0x0808_0000
MIN_IMAGE, MAX_IMAGE = 1024, 512 * 1024


class FirmwareError(Exception):
    """Bad image, bad catalog entry, or a failed flash."""


class FlashBusy(FirmwareError):
    """A flash is already running. Its own type so the HTTP layer can answer
    409 without matching on message text."""


# --- image validation ---------------------------------------------------------

def validate_dfu_image(blob: bytes) -> None:
    """Reject anything that is obviously not a raw STM32F411 image.

    Applied to UPLOADED files only -- a bundled image is covered by its
    sha256, which is strictly stronger. The realistic mistake this catches is
    picking `firmware.elf` or `firmware.hex` out of the build directory
    instead of `firmware.bin`; all three sit side by side and only one of them
    is flashable. Fifteen lines to turn "the board no longer enumerates" into
    a message before anything is written.
    """
    if len(blob) < MIN_IMAGE:
        raise FirmwareError(
            f"image is only {len(blob)} bytes -- too small to be firmware")
    if len(blob) > MAX_IMAGE:
        raise FirmwareError(
            f"image is {len(blob)} bytes, larger than the 512KB flash")

    msp = int.from_bytes(blob[0:4], "little")
    reset = int.from_bytes(blob[4:8], "little")
    if not (SRAM_LO <= msp <= SRAM_HI):
        raise FirmwareError(
            f"not a raw firmware binary: initial stack pointer 0x{msp:08x} is "
            f"outside SRAM. If this is firmware.elf or firmware.hex, use "
            f"firmware.bin instead.")
    if not (FLASH_LO <= reset < FLASH_HI) or not reset & 1:
        raise FirmwareError(
            f"not a raw firmware binary: reset vector 0x{reset:08x} is not a "
            f"Thumb address in flash")


# Same offset/magic-byte reasoning as bundle_firmware.py's check_esp32_image
# (see that function's comment) -- a merged ESP32 image is sparse, with the
# ESP image magic byte at 0x1000, not 0.
ESP32_IMAGE_MAGIC_OFFSET = 0x1000
ESP32_IMAGE_MAGIC = 0xE9


def validate_esp32_image(blob: bytes) -> None:
    """Reject anything that is obviously not a raw merged ESP32 image.

    Applied to UPLOADED files only, same as validate_dfu_image() -- a
    bundled image is covered by its sha256 instead.
    """
    if len(blob) < ESP32_IMAGE_MAGIC_OFFSET + 1:
        raise FirmwareError(
            f"image is only {len(blob)} bytes -- too small to contain a "
            f"bootloader image at offset 0x{ESP32_IMAGE_MAGIC_OFFSET:x}")
    # No upper bound here, unlike validate_dfu_image(): MAX_IMAGE (512KB) is
    # the STM32F411's flash size, not the ESP32's. A real merged ESP32 image
    # (bootloader + partition table + boot_app0.bin + app) is routinely
    # 800KB+ -- this project's own esp32_wroom32 build measures 826432 bytes
    # -- well past 512KB but nowhere near the ESP32's actual 4MB flash.
    # check_esp32_image() in bundle_firmware.py has the same reasoning and
    # the same absence of an upper bound.
    if blob[ESP32_IMAGE_MAGIC_OFFSET] != ESP32_IMAGE_MAGIC:
        raise FirmwareError(
            f"not a raw merged esptool image: byte at offset "
            f"0x{ESP32_IMAGE_MAGIC_OFFSET:x} is not the ESP image magic "
            f"(0x{ESP32_IMAGE_MAGIC:02x})")


# --- the bundle ---------------------------------------------------------------

class Catalog:
    """The firmware images this app shipped with.

    Read fresh from disk on every call rather than cached at import: the
    manifest is a release artifact, and a developer who re-runs
    `app/tools/bundle_firmware.py` while the server is up should see the new
    image without restarting it.
    """

    def __init__(self, bundle_dir: Path | None = None):
        self._dir = Path(bundle_dir) if bundle_dir else BUNDLE_DIR

    @property
    def manifest_path(self) -> Path:
        return self._dir / "manifest.json"

    def _load(self) -> dict:
        if not self.manifest_path.is_file():
            # Not an error, and the common case in a source checkout:
            # app/firmware/ is gitignored build output, so it stays empty
            # until someone runs app/tools/bundle_firmware.py. (A fork that
            # ships no firmware at all lands here too.) The UI says so, and
            # the Advanced upload path still works.
            return {"app_version": None, "images": []}
        try:
            data = json.loads(self.manifest_path.read_text())
        except json.JSONDecodeError as exc:
            raise FirmwareError(f"firmware manifest is not valid JSON: {exc}") from exc
        if not isinstance(data.get("images"), list):
            raise FirmwareError("firmware manifest has no `images` list")
        return data

    def images(self) -> list[dict]:
        """Catalog entries, each annotated with whether its file is present."""
        out = []
        for img in self._load()["images"]:
            entry = dict(img)
            path = self._dir / img.get("file", "")
            entry["available"] = path.is_file()
            out.append(entry)
        return out

    def app_version(self) -> str | None:
        return self._load().get("app_version")

    def get(self, image_id: str) -> dict:
        for img in self._load()["images"]:
            if img.get("id") == image_id:
                return img
        raise FirmwareError(f"no firmware image {image_id!r} in the bundle")

    def verify(self, image_id: str) -> Path:
        """Re-check size and sha256 on disk, then return the path.

        Checked at flash time rather than trusted from the manifest, because
        the manifest and the binary are two files that can drift apart -- a
        bad merge, a partial checkout, an LFS pointer that never resolved.
        The cost is hashing 85KB; the alternative is writing whatever happens
        to be on disk into a device's flash.
        """
        img = self.get(image_id)
        path = self._dir / img["file"]
        if not path.is_file():
            raise FirmwareError(
                f"{img['file']} is missing from the bundle -- the manifest "
                f"lists it but the file is not there")
        blob = path.read_bytes()
        if len(blob) != img["size"]:
            raise FirmwareError(
                f"{img['file']} is {len(blob)} bytes, manifest says {img['size']}")
        digest = hashlib.sha256(blob).hexdigest()
        if digest != img["sha256"]:
            raise FirmwareError(
                f"{img['file']} failed its checksum -- the bundled file does "
                f"not match the manifest and will not be flashed")
        return path


# --- running dfu-util ---------------------------------------------------------

class _Process:
    """Line-ish output from a running subprocess, plus its exit status.

    `dfu-util` draws its progress bar with carriage returns, not newlines, so
    reading by line would deliver the entire download as one enormous line at
    the very end -- i.e. no progress at all. Reading in small chunks and
    splitting on both terminators is what makes the bar move.
    """

    def __init__(self, argv: list[str], env: dict | None = None):
        self._proc = subprocess.Popen(
            argv, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
            text=True, bufsize=0, env=env)

    def lines(self):
        buf = ""
        while True:
            chunk = self._proc.stdout.read(64)
            if not chunk:
                break
            buf += chunk
            parts = re.split(r"[\r\n]", buf)
            buf = parts.pop()          # trailing partial line stays buffered
            for part in parts:
                if part.strip():
                    yield part.strip()
        if buf.strip():
            yield buf.strip()
        self._proc.wait()

    @property
    def returncode(self) -> int:
        return self._proc.returncode


def _default_runner(argv: list[str]) -> _Process:
    return _Process(argv)


# `Found DFU: [0483:df11] ver=2200, devnum=52, ..., alt=0, name="@Internal Flash ...", serial="..."`
_DFU_LINE = re.compile(
    r"\[(?P<vid>[0-9a-fA-F]{4}):(?P<pid>[0-9a-fA-F]{4})\]"
    r"(?:.*?\bdevnum=(?P<devnum>\d+))?"
    r"(?:.*?\balt=(?P<alt>\d+))?"
    r"(?:.*?\bname=\"(?P<name>[^\"]*)\")?"
    r"(?:.*?\bserial=\"(?P<serial>[^\"]*)\")?")

# `Erase   [=========================] 100%        85684 bytes`
# `Download        [=====                    ]  20%        17136 bytes`
#
# Matched as a whole line rather than grepping for a bare "NN%", because
# dfu-util runs TWO independent 0-100% passes -- erase, then download. A plain
# percentage scrape makes one bar reach 100%, snap back to 20% and climb
# again. Carrying the operation name lets the UI say which pass it is showing.
_PROGRESS = re.compile(r"^(?P<op>[A-Za-z]+)\s+\[[=\s]*\]\s+(?P<pct>\d{1,3})%")


class DfuFlasher:
    def __init__(self, runner=None, dfu_util: str = "dfu-util"):
        self._run = runner or _default_runner
        self._dfu_util = dfu_util

    def devices(self) -> list[dict]:
        """Boards currently in DFU mode. Empty list if dfu-util is missing."""
        try:
            proc = self._run([self._dfu_util, "-l"])
            lines = list(proc.lines())
        except FileNotFoundError:
            # A missing dfu-util is a setup problem, not a crash. The UI shows
            # "no DFU device" and the Help page covers installing it; raising
            # here would take out the whole Firmware page instead.
            log.warning("%s not found on PATH", self._dfu_util)
            return []
        except OSError as exc:
            log.warning("could not run %s: %s", self._dfu_util, exc)
            return []

        by_key: dict[str, dict] = {}
        for line in lines:
            m = _DFU_LINE.search(line)
            if not m:
                continue
            if int(m.group("vid"), 16) != DFU_VID or int(m.group("pid"), 16) != DFU_PID:
                continue
            # dfu-util prints one line per alt setting -- four of them for the
            # STM32 ROM bootloader -- but they are one physical board.
            key = m.group("devnum") or m.group("serial") or line
            entry = {
                "vid": m.group("vid"),
                "pid": m.group("pid"),
                "devnum": m.group("devnum"),
                "alt": m.group("alt"),
                "name": m.group("name"),
                "serial": m.group("serial"),
            }
            # Prefer alt 0 when collapsing them. dfu-util lists the alt
            # settings in DESCENDING order, so keeping the first-seen would
            # label the board "@Device Feature/0xFFFF0000" -- while `flash()`
            # writes to `-a 0`, the internal flash. Showing the user one
            # region's name and writing to another is a small lie with an
            # expensive failure mode.
            if key not in by_key or entry["alt"] == "0":
                by_key[key] = entry
        return list(by_key.values())

    def present(self) -> bool:
        return bool(self.devices())

    def wait_for_device(self, timeout: float = 10.0, interval: float = 0.4) -> bool:
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            if self.present():
                return True
            time.sleep(interval)
        return False

    def flash(self, path: Path, on_progress=None) -> None:
        """Write `path` to the board and leave DFU mode.

        `:leave` makes the bootloader reset into the new application when the
        download finishes, so a successful flash ends with the board coming
        back as a normal serial device with no further action.

        `on_progress` receives one dict per output line:
        `{"op": "erase"|"download"|None, "pct": int|None, "line": str}`.
        Lines that carry no progress still come through, so the log the user
        sees is dfu-util's real output rather than a filtered summary.
        """
        argv = [self._dfu_util, "-a", "0",
                "-s", f"0x{FLASH_ORIGIN:08x}:leave", "-D", str(path)]
        try:
            proc = self._run(argv)
        except FileNotFoundError as exc:
            raise FirmwareError(
                f"{self._dfu_util} is not installed or not on PATH") from exc
        except OSError as exc:
            raise FirmwareError(f"could not run {self._dfu_util}: {exc}") from exc

        tail = []
        for line in proc.lines():
            tail.append(line)
            del tail[:-8]
            m = _PROGRESS.match(line)
            if on_progress:
                on_progress({
                    "op": m.group("op").lower() if m else None,
                    "pct": min(100, int(m.group("pct"))) if m else None,
                    "line": line,
                })

        if proc.returncode != 0:
            raise FirmwareError(
                "dfu-util failed (exit {}):\n{}".format(
                    proc.returncode, "\n".join(tail)))


# esptool's real `write-flash` progress line (captured from the installed
# v5.3.1 package's own logger, not assumed): a bracketed bar and a FLOAT
# percent with one decimal --
#   "Writing at 0x00010000 [====>          ]  12.3% 41000/334144 bytes..."
# not the "(NN %)" shape older esptool docs/memory suggest. There is only
# ever one progress source in the default stub-based write path (no separate
# erase-percentage line), so `op` is always "writing" when this matches.
_ESPTOOL_PROGRESS = re.compile(
    r"^Writing at 0x[0-9a-fA-F]+\s+\[.*?\]\s*(?P<pct>\d{1,3}(?:\.\d+)?)%")


def _default_esptool_runner(argv: list[str]) -> _Process:
    # NO_COLOR=1: verified that a color-capable TERM inherited from this
    # process (not tty-ness -- stdout is always a pipe here) makes esptool
    # emit raw ANSI escape codes and \r-based overwrites, which would
    # otherwise land as literal escape bytes in the user-visible log.
    env = dict(os.environ)
    env["NO_COLOR"] = "1"
    return _Process(argv, env=env)


class EsptoolFlasher:
    def __init__(self, runner=None, esptool: str = "esptool"):
        self._run = runner or _default_esptool_runner
        self._esptool = esptool

    def flash(self, path: Path, port: str, on_progress=None) -> None:
        """Write a merged image to `port` and leave esptool's bootloader.

        No devices()/wait_for_device(): unlike a Black Pill in DFU mode, an
        ESP32 in its ROM bootloader has no distinct USB identity to poll
        for. `port` is supplied by the caller (the Firmware page's explicit
        port picker) and esptool performs the reset-into-bootloader
        handshake itself when it opens it.
        """
        argv = [self._esptool, "--chip", "esp32", "--port", port,
                "--baud", "460800", "write-flash", "0x0", str(path)]
        try:
            proc = self._run(argv)
        except FileNotFoundError as exc:
            raise FirmwareError(
                f"{self._esptool} is not installed or not on PATH") from exc
        except OSError as exc:
            raise FirmwareError(f"could not run {self._esptool}: {exc}") from exc

        tail = []
        for line in proc.lines():
            tail.append(line)
            del tail[:-8]
            m = _ESPTOOL_PROGRESS.match(line)
            if on_progress:
                on_progress({
                    "op": "writing" if m else None,
                    "pct": min(100, int(float(m.group("pct")))) if m else None,
                    "line": line,
                })

        if proc.returncode != 0:
            raise FirmwareError(
                "esptool failed (exit {}):\n{}".format(
                    proc.returncode, "\n".join(tail)))


# --- one flash at a time ------------------------------------------------------

class FlashSession:
    """Owns the flashing thread and enforces that only one runs at a time.

    Separate from the HTTP layer so the concurrency rule is testable without
    FastAPI, and so `main.py` only has to forward events to the Broadcaster.
    Two overlapping writes to one device's flash is the kind of thing that
    must be impossible rather than unlikely.
    """

    def __init__(self, flasher, on_event=None):
        self._flasher = flasher
        self._on_event = on_event or (lambda ev: None)
        self._lock = threading.Lock()
        self._thread: threading.Thread | None = None
        self._busy = False

    @property
    def busy(self) -> bool:
        with self._lock:
            return self._busy

    def start(self, path: Path, label: str, flasher=None, wait: bool = True,
             **flash_kwargs) -> None:
        """Start a flash in the background.

        `flasher` defaults to the one this session was constructed with
        (every existing DFU call site keeps working unchanged). Passing a
        different flasher -- e.g. an EsptoolFlasher -- lets one session/one
        busy-lock serve both mechanisms, since the two overlapping-writes
        invariant is about "one flash at a time in this app", not "one flash
        at a time per mechanism". `wait=False` skips the "waiting for a
        device in DFU mode" phase entirely, for a flasher (like
        EsptoolFlasher) with no such concept -- there is nothing to poll for
        an ESP32's bootloader by USB identity. `flash_kwargs` are forwarded
        to `flasher.flash(path, on_progress=..., **flash_kwargs)`, e.g.
        `port=` for EsptoolFlasher.
        """
        with self._lock:
            if self._busy:
                raise FlashBusy("a firmware flash is already in progress")
            self._busy = True
        self._thread = threading.Thread(
            target=self._run,
            args=(path, label, flasher or self._flasher, wait, flash_kwargs),
            daemon=True)
        self._thread.start()

    def _emit(self, phase: str, **fields):
        try:
            self._on_event({"phase": phase, **fields})
        except Exception:
            log.exception("flash event subscriber raised")

    def _run(self, path: Path, label: str, flasher, wait: bool, flash_kwargs: dict):
        try:
            if wait:
                self._emit("waiting", line=f"waiting for a device in DFU mode ({label})")
                if not flasher.wait_for_device():
                    raise FirmwareError(
                        "no device in DFU mode. Hold BOOT0, tap NRST, release "
                        "BOOT0, then try again.")
            self._emit("flashing", pct=0, line=f"writing {label}")
            flasher.flash(
                path,
                on_progress=lambda ev: self._emit("flashing", **ev),
                **flash_kwargs)
            self._emit("done", pct=100, line="flash complete")
        except FirmwareError as exc:
            self._emit("error", line=str(exc))
        except Exception as exc:                      # never kill the thread silently
            log.exception("unexpected error while flashing")
            self._emit("error", line=f"unexpected error: {exc}")
        finally:
            with self._lock:
                self._busy = False
