import json
import time
from pathlib import Path

from fastapi.testclient import TestClient

from backend.device import DeviceModel
from backend.link import SIM_PORT, list_candidate_ports
from backend.main import create_app
from backend.simulator import SimSerial

_REPO = Path(__file__).resolve().parents[2]
GOLDEN = _REPO / "firmware" / "test" / "golden" / "schema.json"
PROFILE = _REPO / "app" / "backend" / "sim_profile.json"


def test_sim_profile_matches_the_firmware_golden_fixture():
    """The simulator's schema is the real firmware's, or it is a lie.

    Regenerate the fixture with `pio test -e native` in firmware/, then
    `cp firmware/test/golden/schema.json app/backend/sim_profile.json`.
    """
    assert json.loads(PROFILE.read_text()) == json.loads(GOLDEN.read_text())


def exchange(sim, req):
    sim.write((json.dumps(req) + "\n").encode())
    while True:
        line = sim.readline()
        assert line, f"no response to {req}"
        msg = json.loads(line)
        if "id" in msg:
            return msg


def test_hello_identifies_itself_as_a_simulator():
    sim = SimSerial(telemetry=False)
    resp = exchange(sim, {"id": 1, "op": "hello"})
    assert resp["ok"] is True
    assert resp["proto"] == 1
    # Not blackpill_f411ce: /api/firmware/catalog derives its recommended
    # image from `board`, and recommending a real flash target for a fake
    # board would be a lie.
    assert resp["board"] == "simulator"
    assert resp["caps"] == []
    sim.close()


def test_schema_is_the_profile():
    sim = SimSerial(telemetry=False)
    resp = exchange(sim, {"id": 1, "op": "schema"})
    assert {p["key"] for p in resp["params"]} == {
        p["key"] for p in json.loads(PROFILE.read_text())["params"]}
    sim.close()


def test_getall_then_get_agree():
    sim = SimSerial(telemetry=False)
    vals = exchange(sim, {"id": 1, "op": "getall"})["vals"]
    one = exchange(sim, {"id": 2, "op": "get", "key": "tlm.rate"})
    assert one["val"] == vals["tlm.rate"]
    sim.close()


def test_set_validates_the_way_the_firmware_does():
    sim = SimSerial(telemetry=False)
    assert exchange(sim, {"id": 1, "op": "set", "key": "tlm.rate", "val": 20})["ok"]
    assert exchange(sim, {"id": 2, "op": "set", "key": "tlm.rate",
                          "val": 99})["err"] == "range"
    assert exchange(sim, {"id": 3, "op": "set", "key": "rx.protocol",
                          "val": "nope"})["err"] == "enum"
    assert exchange(sim, {"id": 4, "op": "set", "key": "tlm.rate",
                          "val": "ten"})["err"] == "badtype"
    assert exchange(sim, {"id": 5, "op": "set", "key": "device.name",
                          "val": "x" * 40})["err"] == "toolong"
    assert exchange(sim, {"id": 6, "op": "set", "key": "no.such",
                          "val": 1})["err"] == "nokey"
    sim.close()


def test_revert_reports_defaults_before_a_save_and_flash_after():
    sim = SimSerial(telemetry=False)
    exchange(sim, {"id": 1, "op": "set", "key": "tlm.rate", "val": 20})
    assert exchange(sim, {"id": 2, "op": "revert"})["src"] == "defaults"
    exchange(sim, {"id": 3, "op": "set", "key": "tlm.rate", "val": 20})
    exchange(sim, {"id": 4, "op": "save"})
    exchange(sim, {"id": 5, "op": "set", "key": "tlm.rate", "val": 40})
    assert exchange(sim, {"id": 6, "op": "revert"})["src"] == "flash"
    assert exchange(sim, {"id": 7, "op": "get", "key": "tlm.rate"})["val"] == 20
    sim.close()


def test_unsupported_ops_answer_the_way_a_board_without_them_does():
    sim = SimSerial(telemetry=False)
    assert exchange(sim, {"id": 1, "op": "dfu"})["err"] == "nodfu"
    assert exchange(sim, {"id": 2, "op": "wifiscan"})["err"] == "nowifi"
    assert exchange(sim, {"id": 3, "op": "nonsense"})["err"] == "badop"
    sim.close()


def test_a_boot_log_line_marks_the_values_as_fabricated():
    sim = SimSerial(telemetry=False)
    sim.write(b'{"id":1,"op":"hello"}\n')
    lines = [json.loads(sim.readline()) for _ in range(2)]
    logs = [msg for msg in lines if "log" in msg]
    assert logs and "simulated" in logs[0]["log"].lower()
    sim.close()


def test_readline_returns_empty_on_timeout_like_pyserial():
    sim = SimSerial(telemetry=False, timeout=0.01)
    assert sim.readline() == b""
    sim.close()


def test_the_simulator_is_always_offered_first_and_never_preselected():
    ports = list_candidate_ports()
    assert ports[0]["port"] == SIM_PORT
    assert ports[0]["sim"] is True
    # match drives the UI's auto-selection: a real board must always win it.
    assert ports[0]["match"] is False
    assert all(p["sim"] is False for p in ports[1:])


def test_connecting_to_the_simulator_serves_schema_and_values():
    device = DeviceModel()
    try:
        device.connect(SIM_PORT)
        assert device.status()["state"] == "connected"
        assert device.status()["board"] == "simulator"
        assert len(device.schema()["params"]) == 29
        assert device.values()["tlm.rate"] == 10
    finally:
        device.disconnect()


def test_the_whole_api_works_against_the_simulator():
    app = create_app(DeviceModel())
    with TestClient(app) as c:
        assert c.post("/api/connect", json={"port": SIM_PORT}).status_code == 200
        assert c.put("/api/params/tlm.rate", json={"val": 25}).status_code == 200
        assert c.get("/api/params").json()["tlm.rate"] == 25
        assert c.post("/api/params/save").json()["ok"] is True
        assert c.post("/api/params/revert").json()["src"] == "flash"
        assert c.post("/api/terminal", json={"command": "get tlm.rate"}).json()["ok"]
        c.post("/api/disconnect")


def _mixed_link():
    """A link where sim://board is the simulator and any other port is a board."""
    from backend.link import SerialLink
    from tests.fake_serial import FakeSerial
    from tests.test_device import device_responder
    return SerialLink(open_port=lambda p: SimSerial(telemetry=False) if p == SIM_PORT
                      else FakeSerial(responder=device_responder()))


def test_a_simulator_session_does_not_erase_the_last_real_board():
    """A board in DFU mode cannot identify itself -- every STM32F4 bootloader
    reports 0483:df11 -- so the Firmware page's image recommendation comes
    entirely from the last successful `hello`. A simulator is not hardware and
    must not be allowed to answer that question.
    """
    device = DeviceModel(_mixed_link())
    try:
        device.connect("/dev/fake")
        assert device.last_real_board() == "blackpill_f411ce"
        device.disconnect()

        device.connect(SIM_PORT)
        assert device.status()["board"] == "simulator"   # honest about now
        assert device.last_real_board() == "blackpill_f411ce"  # remembers hardware
        device.disconnect()

        assert device.last_real_board() == "blackpill_f411ce"
    finally:
        device.disconnect()


def test_the_catalog_reports_the_last_real_board_not_the_simulator():
    device = DeviceModel(_mixed_link())
    app = create_app(device)
    with TestClient(app) as c:
        c.post("/api/connect", json={"port": "/dev/fake"})
        c.post("/api/disconnect")
        c.post("/api/connect", json={"port": SIM_PORT})
        c.post("/api/disconnect")
        assert c.get("/api/firmware/catalog").json()["board"] == "blackpill_f411ce"


def test_telemetry_frames_reach_a_websocket_client():
    app = create_app(DeviceModel())
    with TestClient(app) as c:
        c.post("/api/connect", json={"port": SIM_PORT})
        with c.websocket_connect("/ws") as ws:
            deadline = time.monotonic() + 5.0
            while time.monotonic() < deadline:
                msg = ws.receive_json()
                if msg["type"] == "tlm":
                    assert msg["data"]["ch1"] > 0
                    break
            else:
                raise AssertionError("no telemetry frame within 5s")
        c.post("/api/disconnect")
