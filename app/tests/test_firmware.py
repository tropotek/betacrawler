"""Catalog + dfu-util wrapper, exercised with no board and no dfu-util.

The `runner` seam is what makes this possible: every test below drives real
`dfu-util` output captured from the tool's actual format, replayed through a
fake process. The one exception is test_process_splits_on_carriage_returns,
which runs a real subprocess on purpose -- the carriage-return handling is the
part a fake could trivially get wrong in the same way the code does.
"""
import hashlib
import json
import threading
import time

import pytest

from backend.firmware import (
    Catalog, DfuFlasher, FlashSession, FirmwareError, _Process,
    validate_image, FLASH_ORIGIN,
)


# --- fakes --------------------------------------------------------------------

class FakeProcess:
    def __init__(self, lines, rc=0):
        self._lines = list(lines)
        self._rc = rc

    def lines(self):
        yield from self._lines

    @property
    def returncode(self):
        return self._rc


def runner_for(lines, rc=0, record=None):
    def run(argv):
        if record is not None:
            record.append(argv)
        return FakeProcess(lines, rc)
    return run


# Verbatim shape of `dfu-util -l` with a Black Pill in DFU mode: the ROM
# bootloader advertises four alt settings, all on one physical device.
DFU_LIST = [
    "dfu-util 0.11",
    "Copyright 2005-2009 Weston Schmidt, Harald Welte and OpenMoko Inc.",
    'Found DFU: [0483:df11] ver=2200, devnum=52, cfg=1, intf=0, path="1-1", '
    'alt=3, name="@Device Feature/0xFFFF0000/01*004 e", serial="336B357C3232"',
    'Found DFU: [0483:df11] ver=2200, devnum=52, cfg=1, intf=0, path="1-1", '
    'alt=2, name="@OTP Memory /0x1FFF7800/01*512 e,01*016 e", serial="336B357C3232"',
    'Found DFU: [0483:df11] ver=2200, devnum=52, cfg=1, intf=0, path="1-1", '
    'alt=1, name="@Option Bytes  /0x1FFFC000/01*016 e", serial="336B357C3232"',
    'Found DFU: [0483:df11] ver=2200, devnum=52, cfg=1, intf=0, path="1-1", '
    'alt=0, name="@Internal Flash  /0x08000000/04*016Kg,01*064Kg,03*128Kg", '
    'serial="336B357C3232"',
]

DFU_DOWNLOAD = [
    "dfu-util 0.11",
    "Warning: Invalid DFU suffix signature",
    "Opening DFU capable USB device...",
    "Device ID 0483:df11",
    "Downloading element to address = 0x08000000, size = 85684",
    "Erase   [=========================] 100%        85684 bytes",
    "Erase    done.",
    "Download        [=====                    ]  20%        17136 bytes",
    "Download        [=============            ]  53%        45400 bytes",
    "Download        [=========================] 100%        85684 bytes",
    "Download done.",
    "File downloaded successfully",
]


def make_image(size=2048, msp=0x2002_0000, reset=0x0800_ee59):
    """A minimally plausible firmware image."""
    return (msp.to_bytes(4, "little") + reset.to_bytes(4, "little")
            + b"\x00" * (size - 8))


# --- image validation ---------------------------------------------------------

def test_validate_accepts_a_plausible_image():
    validate_image(make_image())


def test_validate_accepts_msp_at_the_very_top_of_sram():
    # Not a hypothetical: a real silkscreen build has MSP == 0x20020000 exactly.
    # An exclusive upper bound here would reject every genuine image.
    validate_image(make_image(msp=0x2002_0000))


def test_validate_rejects_an_elf():
    with pytest.raises(FirmwareError, match="stack pointer"):
        validate_image(b"\x7fELF" + b"\x00" * 4096)


def test_validate_rejects_a_bad_reset_vector():
    with pytest.raises(FirmwareError, match="reset vector"):
        validate_image(make_image(reset=0x2000_0000))


def test_validate_rejects_a_non_thumb_reset_vector():
    with pytest.raises(FirmwareError, match="reset vector"):
        validate_image(make_image(reset=0x0800_ee58))   # bit 0 clear


def test_validate_rejects_tiny_and_oversized():
    with pytest.raises(FirmwareError, match="too small"):
        validate_image(b"\x00" * 16)
    with pytest.raises(FirmwareError, match="larger than"):
        validate_image(make_image(size=513 * 1024))


# --- catalog ------------------------------------------------------------------

@pytest.fixture
def bundle(tmp_path):
    blob = make_image()
    (tmp_path / "boardx").mkdir()
    (tmp_path / "boardx" / "fw-1.0.0.bin").write_bytes(blob)
    (tmp_path / "manifest.json").write_text(json.dumps({
        "app_version": "1.0.0",
        "images": [{
            "id": "boardx-fw-1.0.0", "board": "boardx", "name": "fw",
            "version": "1.0.0", "built": "Jul 26 2026 15:02:35", "proto": 1,
            "method": "dfu", "file": "boardx/fw-1.0.0.bin",
            "size": len(blob), "sha256": hashlib.sha256(blob).hexdigest(),
            "notes": "led, button",
        }],
    }))
    return tmp_path


def test_catalog_lists_images(bundle):
    images = Catalog(bundle).images()
    assert len(images) == 1
    assert images[0]["id"] == "boardx-fw-1.0.0"
    assert images[0]["available"] is True


def test_catalog_marks_a_missing_file_unavailable(bundle):
    (bundle / "boardx" / "fw-1.0.0.bin").unlink()
    assert Catalog(bundle).images()[0]["available"] is False


def test_catalog_verify_returns_the_path(bundle):
    assert Catalog(bundle).verify("boardx-fw-1.0.0").is_file()


def test_catalog_verify_rejects_a_corrupted_file(bundle):
    path = bundle / "boardx" / "fw-1.0.0.bin"
    blob = bytearray(path.read_bytes())
    blob[100] ^= 0xFF                       # same length, different content
    path.write_bytes(bytes(blob))
    with pytest.raises(FirmwareError, match="checksum"):
        Catalog(bundle).verify("boardx-fw-1.0.0")


def test_catalog_verify_rejects_a_wrong_size_file(bundle):
    (bundle / "boardx" / "fw-1.0.0.bin").write_bytes(make_image(size=4096))
    with pytest.raises(FirmwareError, match="bytes, manifest says"):
        Catalog(bundle).verify("boardx-fw-1.0.0")


def test_catalog_verify_reports_a_missing_file(bundle):
    (bundle / "boardx" / "fw-1.0.0.bin").unlink()
    with pytest.raises(FirmwareError, match="missing from the bundle"):
        Catalog(bundle).verify("boardx-fw-1.0.0")


def test_catalog_unknown_id(bundle):
    with pytest.raises(FirmwareError, match="no firmware image"):
        Catalog(bundle).get("nope")


def test_catalog_missing_manifest_is_empty_not_an_error(tmp_path):
    assert Catalog(tmp_path).images() == []


def test_catalog_rejects_broken_json(tmp_path):
    (tmp_path / "manifest.json").write_text("{not json")
    with pytest.raises(FirmwareError, match="not valid JSON"):
        Catalog(tmp_path).images()


def test_catalog_rereads_manifest_between_calls(bundle):
    """Re-bundling while the server runs must show up without a restart."""
    cat = Catalog(bundle)
    assert len(cat.images()) == 1
    data = json.loads((bundle / "manifest.json").read_text())
    data["images"] = []
    (bundle / "manifest.json").write_text(json.dumps(data))
    assert cat.images() == []


# --- device listing -----------------------------------------------------------

def test_devices_dedupes_alt_settings():
    flasher = DfuFlasher(runner=runner_for(DFU_LIST))
    devices = flasher.devices()
    assert len(devices) == 1                  # four alt lines, one board
    assert devices[0]["devnum"] == "52"
    assert devices[0]["serial"] == "336B357C3232"


def test_devices_reports_the_alt_setting_that_gets_flashed():
    """dfu-util lists alt settings descending, so alt=3 comes first.

    flash() writes to `-a 0`. Reporting any other region's name would show
    the user "@Device Feature/0xFFFF0000" while writing to internal flash.
    """
    device = DfuFlasher(runner=runner_for(DFU_LIST)).devices()[0]
    assert device["alt"] == "0"
    assert "Internal Flash" in device["name"]


def test_devices_empty_when_nothing_in_dfu():
    flasher = DfuFlasher(runner=runner_for(["dfu-util 0.11", "No DFU capable USB device available"]))
    assert flasher.devices() == []
    assert flasher.present() is False


def test_devices_ignores_other_vendors():
    lines = ['Found DFU: [1234:5678] ver=0100, devnum=9, alt=0, name="x", serial="y"']
    assert DfuFlasher(runner=runner_for(lines)).devices() == []


def test_devices_survives_missing_dfu_util():
    def missing(argv):
        raise FileNotFoundError("dfu-util")
    # A missing tool must degrade to "no device", not take out the page.
    assert DfuFlasher(runner=missing).devices() == []


# --- flashing -----------------------------------------------------------------

def test_flash_builds_the_right_command(tmp_path):
    calls = []
    image = tmp_path / "fw.bin"
    image.write_bytes(make_image())
    DfuFlasher(runner=runner_for(DFU_DOWNLOAD, record=calls)).flash(image)
    assert calls[0] == [
        "dfu-util", "-a", "0", "-s", f"0x{FLASH_ORIGIN:08x}:leave",
        "-D", str(image),
    ]


def test_flash_reports_download_progress_monotonically(tmp_path):
    seen = []
    image = tmp_path / "fw.bin"
    image.write_bytes(make_image())
    DfuFlasher(runner=runner_for(DFU_DOWNLOAD)).flash(
        image, on_progress=seen.append)

    download = [e["pct"] for e in seen if e["op"] == "download"]
    assert download == [20, 53, 100]
    # The erase pass has its own 0-100%. Tagging it separately is what stops
    # a single bar from hitting 100%, snapping back to 20% and climbing again.
    assert [e["pct"] for e in seen if e["op"] == "erase"] == [100]


def test_flash_passes_through_non_progress_lines(tmp_path):
    """The user sees dfu-util's real output, not a filtered summary --
    including the "Invalid DFU suffix" warning a raw .bin always produces."""
    seen = []
    image = tmp_path / "fw.bin"
    image.write_bytes(make_image())
    DfuFlasher(runner=runner_for(DFU_DOWNLOAD)).flash(
        image, on_progress=seen.append)

    plain = [e["line"] for e in seen if e["pct"] is None]
    assert any("Opening DFU" in line for line in plain)
    assert any("Invalid DFU suffix" in line for line in plain)


def test_flash_raises_on_nonzero_exit(tmp_path):
    image = tmp_path / "fw.bin"
    image.write_bytes(make_image())
    lines = ["Opening DFU capable USB device...",
             "dfu-util: Cannot open DFU device 0483:df11"]
    with pytest.raises(FirmwareError, match="Cannot open DFU device"):
        DfuFlasher(runner=runner_for(lines, rc=74)).flash(image)


def test_flash_reports_a_missing_dfu_util(tmp_path):
    image = tmp_path / "fw.bin"
    image.write_bytes(make_image())

    def missing(argv):
        raise FileNotFoundError("dfu-util")
    with pytest.raises(FirmwareError, match="not installed"):
        DfuFlasher(runner=missing).flash(image)


def test_process_splits_on_carriage_returns():
    """The real subprocess wrapper, not a fake.

    dfu-util draws its progress bar with \\r. Reading by line would hold the
    whole download back and deliver it as one line at the end -- i.e. a
    progress bar that jumps straight from 0 to 100.
    """
    proc = _Process(["printf", "a 10%%\rb 50%%\rc 100%%\n"])
    assert list(proc.lines()) == ["a 10%", "b 50%", "c 100%"]
    assert proc.returncode == 0


# --- session ------------------------------------------------------------------

def test_session_emits_a_full_event_sequence(tmp_path):
    image = tmp_path / "fw.bin"
    image.write_bytes(make_image())
    events = []
    flasher = DfuFlasher(runner=runner_for(DFU_LIST))
    flasher.flash = lambda path, on_progress=None: on_progress(
        {"op": "download", "pct": 100, "line": "Download done."})
    session = FlashSession(flasher, on_event=events.append)

    session.start(image, "fw 1.0.0")
    _join(session)

    phases = [e["phase"] for e in events]
    assert phases[0] == "waiting"
    assert "flashing" in phases
    assert phases[-1] == "done"


def test_session_reports_failure_as_an_error_event(tmp_path):
    image = tmp_path / "fw.bin"
    image.write_bytes(make_image())
    events = []

    def boom(path, on_progress=None):
        raise FirmwareError("device went away mid-write")
    flasher = DfuFlasher(runner=runner_for(DFU_LIST))
    flasher.flash = boom
    session = FlashSession(flasher, on_event=events.append)

    session.start(image, "fw 1.0.0")
    _join(session)

    assert events[-1]["phase"] == "error"
    assert "went away" in events[-1]["line"]
    assert session.busy is False              # and the session is reusable


def test_session_reports_no_dfu_device(tmp_path):
    image = tmp_path / "fw.bin"
    image.write_bytes(make_image())
    events = []
    flasher = DfuFlasher(runner=runner_for(["No DFU capable USB device available"]))
    flasher.wait_for_device = lambda timeout=10.0, interval=0.4: False
    session = FlashSession(flasher, on_event=events.append)

    session.start(image, "fw 1.0.0")
    _join(session)

    assert events[-1]["phase"] == "error"
    assert "BOOT0" in events[-1]["line"]      # tells you how to fix it


def test_session_refuses_a_concurrent_flash(tmp_path):
    image = tmp_path / "fw.bin"
    image.write_bytes(make_image())
    release = threading.Event()
    flasher = DfuFlasher(runner=runner_for(DFU_LIST))
    flasher.flash = lambda path, on_progress=None: release.wait(2.0)
    session = FlashSession(flasher)

    session.start(image, "first")
    try:
        # Two overlapping writes to one device's flash must be impossible,
        # not merely unlikely.
        with pytest.raises(FirmwareError, match="already in progress"):
            session.start(image, "second")
    finally:
        release.set()
    _join(session)
    assert session.busy is False


def _join(session, timeout=3.0):
    deadline = time.monotonic() + timeout
    while session.busy and time.monotonic() < deadline:
        time.sleep(0.01)
    assert not session.busy, "flash thread did not finish"
