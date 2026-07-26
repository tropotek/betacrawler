"""HTTP surface for the firmware/DFU routes.

Kept apart from test_api.py because these need their own app fixture: a
Catalog pointed at a temporary bundle and a DfuFlasher backed by canned
dfu-util output, injected through create_app(). The point is that nothing
here touches a board, dfu-util, or the app's real app/firmware/ directory.
"""
import hashlib
import json
import queue
import threading
import time

import pytest
from fastapi.testclient import TestClient

from backend.link import SerialLink
from backend.device import DeviceModel
from backend.firmware import Catalog, DfuFlasher
from backend.main import create_app
from tests.fake_serial import FakeSerial
from tests.test_device import device_responder
from tests.test_firmware import DFU_LIST, DFU_DOWNLOAD, make_image, runner_for


@pytest.fixture
def bundle(tmp_path):
    blob = make_image()
    (tmp_path / "blackpill_f411ce").mkdir()
    (tmp_path / "blackpill_f411ce" / "silkscreen-1.0.0.bin").write_bytes(blob)
    (tmp_path / "manifest.json").write_text(json.dumps({
        "app_version": "1.0.0",
        "images": [{
            "id": "blackpill_f411ce-silkscreen-1.0.0",
            "board": "blackpill_f411ce", "name": "silkscreen",
            "version": "1.0.0", "built": "Jul 26 2026 15:02:35", "proto": 1,
            "method": "dfu", "file": "blackpill_f411ce/silkscreen-1.0.0.bin",
            "size": len(blob), "sha256": hashlib.sha256(blob).hexdigest(),
            "notes": "led, button, st7789_240x240",
        }, {
            # A second board, so "recommended" has something to choose wrong.
            "id": "otherboard-silkscreen-1.0.0",
            "board": "otherboard", "name": "silkscreen",
            "version": "1.0.0", "built": "Jul 26 2026 15:02:35", "proto": 1,
            "method": "dfu", "file": "otherboard/silkscreen-1.0.0.bin",
            "size": len(blob), "sha256": hashlib.sha256(blob).hexdigest(),
            "notes": "led",
        }],
    }))
    return tmp_path


def make_client(bundle, dfu_lines=DFU_LIST, flash_lines=DFU_DOWNLOAD,
                flash_rc=0, caps=("dfu",)):
    fake = FakeSerial(responder=device_responder(caps=caps))
    device = DeviceModel(SerialLink(open_port=lambda p: fake))

    def runner(argv):
        from tests.test_firmware import FakeProcess
        if "-D" in argv:
            # Failure is keyed off dfu-util's EXIT CODE, never its output
            # text -- so a failing case has to supply one, exactly as the
            # real tool would.
            return FakeProcess(flash_lines, rc=flash_rc)
        return FakeProcess(dfu_lines)

    app = create_app(device,
                     catalog=Catalog(bundle),
                     flasher=DfuFlasher(runner=runner))
    app.state.fake = fake
    app.state.device = device
    return app


@pytest.fixture
def client(bundle):
    app = make_client(bundle)
    with TestClient(app) as c:
        yield c
    app.state.device.disconnect()


# --- catalog ------------------------------------------------------------------

def test_catalog_lists_bundled_images(client):
    body = client.get("/api/firmware/catalog").json()
    assert body["app_version"] == "1.0.0"
    assert {i["id"] for i in body["images"]} == {
        "blackpill_f411ce-silkscreen-1.0.0", "otherboard-silkscreen-1.0.0"}


def test_catalog_marks_a_missing_binary_unavailable(client):
    # otherboard's manifest entry has no file on disk.
    images = {i["id"]: i for i in client.get("/api/firmware/catalog").json()["images"]}
    assert images["blackpill_f411ce-silkscreen-1.0.0"]["available"] is True
    assert images["otherboard-silkscreen-1.0.0"]["available"] is False


def test_no_recommendation_before_a_board_has_been_seen(client):
    """A board in DFU reports 0483:df11 and nothing else -- it cannot say what
    it is. With nothing connected there is no honest recommendation to make."""
    body = client.get("/api/firmware/catalog").json()
    assert body["board"] is None
    assert body["recommended"] is None


def test_recommendation_follows_the_connected_board(client):
    client.post("/api/connect", json={"port": "/dev/fake"})
    body = client.get("/api/firmware/catalog").json()
    assert body["board"] == "blackpill_f411ce"
    assert body["recommended"] == "blackpill_f411ce-silkscreen-1.0.0"


def test_an_unavailable_image_is_never_recommended(bundle):
    """The manifest lists otherboard's image but the file is absent."""
    app = make_client(bundle)
    with TestClient(app) as c:
        c.post("/api/connect", json={"port": "/dev/fake"})
        # Point the device at the board whose binary is missing.
        app.state.device._info["board"] = "otherboard"
        assert c.get("/api/firmware/catalog").json()["recommended"] is None
    app.state.device.disconnect()


# --- dfu status ---------------------------------------------------------------

def test_dfu_status_reports_a_waiting_board(client):
    body = client.get("/api/firmware/dfu-status").json()
    assert body["present"] is True
    assert body["devices"][0]["alt"] == "0"      # the region we actually write
    assert body["busy"] is False


def test_dfu_status_with_nothing_in_dfu(bundle):
    app = make_client(bundle, dfu_lines=["No DFU capable USB device available"])
    with TestClient(app) as c:
        body = c.get("/api/firmware/dfu-status").json()
        assert body["present"] is False
        assert body["devices"] == []
    app.state.device.disconnect()


# --- enter dfu ----------------------------------------------------------------

def test_enter_dfu_requires_a_connection(client):
    r = client.post("/api/firmware/enter-dfu")
    assert r.status_code == 409
    assert r.json()["err"] == "disconnected"


def test_enter_dfu_acks_then_drops_the_link(client):
    client.post("/api/connect", json={"port": "/dev/fake"})
    r = client.post("/api/firmware/enter-dfu")
    assert r.status_code == 200
    assert r.json()["ok"] is True
    # The board is rebooting; the port is gone. Reporting it as still
    # connected would leave the UI offering controls that cannot work.
    assert r.json()["state"] == "disconnected"


def test_enter_dfu_on_firmware_built_without_it(bundle):
    app = make_client(bundle, caps=())
    with TestClient(app) as c:
        c.post("/api/connect", json={"port": "/dev/fake"})
        r = c.post("/api/firmware/enter-dfu")
        assert r.status_code == 400
        assert r.json()["err"] == "nodfu"
    app.state.device.disconnect()


# --- flashing -----------------------------------------------------------------

def test_flash_a_bundled_image(client):
    r = client.post("/api/firmware/flash",
                    json={"id": "blackpill_f411ce-silkscreen-1.0.0"})
    assert r.status_code == 200
    assert r.json()["ok"] is True
    _settle(client)


def test_flash_an_unknown_id(client):
    r = client.post("/api/firmware/flash", json={"id": "nope"})
    assert r.status_code == 400
    assert r.json()["err"] == "firmware"
    assert "no firmware image" in r.json()["detail"]


def test_flash_rejects_a_corrupted_bundled_image(client, bundle):
    path = bundle / "blackpill_f411ce" / "silkscreen-1.0.0.bin"
    blob = bytearray(path.read_bytes())
    blob[64] ^= 0xFF
    path.write_bytes(bytes(blob))
    r = client.post("/api/firmware/flash",
                    json={"id": "blackpill_f411ce-silkscreen-1.0.0"})
    assert r.status_code == 400
    assert "checksum" in r.json()["detail"]


def test_upload_flash_accepts_a_real_looking_image(client):
    r = client.post("/api/firmware/flash-upload?filename=firmware.bin",
                    content=make_image())
    assert r.status_code == 200
    assert r.json()["filename"] == "firmware.bin"
    _settle(client)


def test_upload_flash_rejects_an_elf(client):
    """The realistic mistake: firmware.elf and firmware.bin sit side by side
    in .pio/build and only one of them is flashable."""
    r = client.post("/api/firmware/flash-upload?filename=firmware.elf",
                    content=b"\x7fELF" + b"\x00" * 4096)
    assert r.status_code == 400
    assert "stack pointer" in r.json()["detail"]


def test_upload_flash_rejects_junk(client):
    r = client.post("/api/firmware/flash-upload", content=b"hello")
    assert r.status_code == 400
    assert "too small" in r.json()["detail"]


def test_progress_reaches_the_websocket_as_flash_frames(client):
    """Flash progress must arrive as its own frame type.

    Not `log`: app.js renders log frames in the Terminal as `[device] …`, and
    this output comes from dfu-util on the host, not from the board.
    """
    with client.websocket_connect("/ws") as ws:
        assert ws.receive_json()["type"] == "state"
        client.post("/api/firmware/flash",
                    json={"id": "blackpill_f411ce-silkscreen-1.0.0"})

        frames = _drain(ws, until=lambda f: f["data"].get("phase") == "done")

    assert all(f["type"] == "flash" for f in frames)
    phases = [f["data"]["phase"] for f in frames]
    assert phases[0] == "waiting"
    assert phases[-1] == "done"
    download = [f["data"]["pct"] for f in frames if f["data"].get("op") == "download"]
    assert download == [20, 53, 100]


def test_a_failed_flash_reports_an_error_frame(bundle):
    app = make_client(bundle, flash_lines=["dfu-util: Cannot open DFU device"],
                      flash_rc=74)
    with TestClient(app) as c:
        with c.websocket_connect("/ws") as ws:
            assert ws.receive_json()["type"] == "state"
            c.post("/api/firmware/flash",
                   json={"id": "blackpill_f411ce-silkscreen-1.0.0"})
            frames = _drain(ws, until=lambda f: f["data"].get("phase") == "error")
        assert frames[-1]["data"]["phase"] == "error"
    app.state.device.disconnect()


def test_two_flashes_at_once_is_a_conflict(bundle):
    """Two overlapping writes to one device's flash must be impossible."""
    release = threading.Event()

    class SlowProcess:
        def lines(self):
            release.wait(2.0)
            yield "Download        [====] 100%   2048 bytes"

        @property
        def returncode(self):
            return 0

    def runner(argv):
        if "-D" in argv:
            return SlowProcess()
        from tests.test_firmware import FakeProcess
        return FakeProcess(DFU_LIST)

    app = create_app(DeviceModel(SerialLink(open_port=lambda p: FakeSerial())),
                     catalog=Catalog(bundle),
                     flasher=DfuFlasher(runner=runner))
    with TestClient(app) as c:
        first = c.post("/api/firmware/flash",
                       json={"id": "blackpill_f411ce-silkscreen-1.0.0"})
        assert first.status_code == 200
        _wait_busy(c)
        second = c.post("/api/firmware/flash",
                        json={"id": "blackpill_f411ce-silkscreen-1.0.0"})
        assert second.status_code == 409
        assert second.json()["err"] == "busy"
        release.set()
        _settle(c)


# --- helpers ------------------------------------------------------------------

def _drain(ws, until, timeout=3.0):
    """Collect WS frames until `until` matches. Bounded, so a dropped frame
    fails the test instead of hanging the run (same reasoning as the
    broadcaster tests in test_api.py)."""
    frames = []
    result: "queue.Queue" = queue.Queue()

    def _recv():
        try:
            while True:
                result.put(("ok", ws.receive_json()))
        except Exception as exc:
            result.put(("err", exc))

    threading.Thread(target=_recv, daemon=True).start()
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        try:
            kind, payload = result.get(timeout=0.2)
        except queue.Empty:
            continue
        if kind == "err":
            pytest.fail(f"websocket raised: {payload!r}")
        frames.append(payload)
        if until(payload):
            return frames
    pytest.fail(f"no matching websocket frame within {timeout}s; got {frames!r}")


def _wait_busy(client, timeout=2.0):
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        if client.get("/api/firmware/dfu-status").json()["busy"]:
            return
        time.sleep(0.02)
    pytest.fail("flash never became busy")


def _settle(client, timeout=3.0):
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        if not client.get("/api/firmware/dfu-status").json()["busy"]:
            return
        time.sleep(0.02)
    pytest.fail("flash never finished")


def test_dfu_status_does_not_enumerate_during_a_flash(bundle):
    """`dfu-util -l` opens the USB device. The UI polls this route every 1.5s,
    so enumerating mid-download would have a second process claiming the same
    interface while firmware is being written -- an intermittent corruption
    risk, and the hardest kind of bug to reproduce."""
    release = threading.Event()
    downloading = threading.Event()
    list_calls = []

    class SlowProcess:
        def lines(self):
            downloading.set()          # dfu-util now holds the device
            release.wait(3.0)
            yield "Download        [====] 100%   2048 bytes"

        @property
        def returncode(self):
            return 0

    def runner(argv):
        if "-D" in argv:
            return SlowProcess()
        list_calls.append(argv)
        from tests.test_firmware import FakeProcess
        return FakeProcess(DFU_LIST)

    app = create_app(DeviceModel(SerialLink(open_port=lambda p: FakeSerial())),
                     catalog=Catalog(bundle),
                     flasher=DfuFlasher(runner=runner))
    with TestClient(app) as c:
        c.get("/api/firmware/dfu-status")          # primes the cache
        c.post("/api/firmware/flash",
               json={"id": "blackpill_f411ce-silkscreen-1.0.0"})
        # The session's own wait_for_device() legitimately enumerates first;
        # what must never happen is enumerating once the download is under way.
        assert downloading.wait(3.0), "download never started"
        before = len(list_calls)

        for _ in range(5):
            body = c.get("/api/firmware/dfu-status").json()
            assert body["busy"] is True
            # Still reported as present -- the board really is in DFU, we
            # simply refuse to go and ask it again right now.
            assert body["present"] is True
            assert body["devices"][0]["alt"] == "0"

        assert len(list_calls) == before, (
            "dfu-util -l ran while a flash held the device")

        release.set()
        _settle(c)
        # Once the flash is done, enumeration resumes.
        c.get("/api/firmware/dfu-status")
        assert len(list_calls) > before
