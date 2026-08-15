import json
from pathlib import Path

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
