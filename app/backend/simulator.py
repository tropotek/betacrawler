"""A simulated board behind the real JSON-lines wire protocol.

Implements the slice of serial.Serial that SerialLink uses, so the device
model, HTTP routes, WebSocket push, Terminal and INI restore all run against
it unmodified. Values come from sim_model and are fabricated.
"""
import json
import queue
import threading
import time
from pathlib import Path

from .sim_model import SimModel

PROFILE_PATH = Path(__file__).resolve().parent / "sim_profile.json"

SIM_FW = "betacrawler 1.0.0 (sim)"
SIM_NAME = "betacrawler"
SIM_VER = "1.0.0"
SIM_BOARD = "simulator"
SIM_MODS = ["device", "system", "rx", "tank_drive", "esc0", "esc1"]
PROTO_VERSION = 1
BOOT_LOG = "simulated board - every value below is fabricated"


def load_profile() -> dict:
    return json.loads(PROFILE_PATH.read_text())


def _validate(spec: dict, val):
    """Mirrors the firmware's own validation. It never trusts its host, and
    neither does this -- the backend's check is a separate, earlier one."""
    kind = spec["type"]
    if kind == "u8":
        if not isinstance(val, int) or isinstance(val, bool):
            return "badtype"
        if val < spec["min"] or val > spec["max"]:
            return "range"
    elif kind == "enum":
        if val not in spec["options"]:
            return "enum"
    elif kind == "str":
        if not isinstance(val, str):
            return "badtype"
        if len(val) > spec["maxlen"]:
            return "toolong"
    return None


class SimSerial:
    """serial.Serial's write/readline/close surface, backed by SimModel."""

    def __init__(self, telemetry: bool = True, timeout: float = 0.2,
                 clock=time.monotonic):
        self._clock = clock
        self._t0 = clock()
        self._profile = load_profile()
        self._model = SimModel(self._profile["params"])
        self._lines: queue.Queue = queue.Queue()
        self._lock = threading.Lock()
        self._stop = threading.Event()
        self._tlm_on = True
        self.timeout = timeout
        self.is_open = True
        self._thread = None
        if telemetry:
            self._thread = threading.Thread(target=self._tlm_loop, daemon=True)
            self._thread.start()

    # --- serial.Serial surface ---------------------------------------------
    def write(self, data: bytes) -> int:
        if not self.is_open:
            raise OSError("simulated port closed")
        try:
            req = json.loads(data.decode())
        except (ValueError, UnicodeDecodeError):
            return len(data)
        with self._lock:
            resp = self._handle(req)
        self._emit(resp)
        if req.get("op") == "hello":
            self._emit({"log": BOOT_LOG})
        return len(data)

    def readline(self) -> bytes:
        # Empty rather than an error once closed, so SerialLink's reader
        # thread leaves through its own stop flag instead of reporting a
        # lost port during an ordinary disconnect.
        if not self.is_open:
            return b""
        try:
            return self._lines.get(timeout=self.timeout)
        except queue.Empty:
            return b""

    def close(self) -> None:
        self.is_open = False
        self._stop.set()
        if self._thread is not None:
            self._thread.join(timeout=1.0)

    # --- internals ----------------------------------------------------------
    def _now_ms(self) -> int:
        return int((self._clock() - self._t0) * 1000)

    def _emit(self, obj: dict) -> None:
        self._lines.put(json.dumps(obj, separators=(",", ":")).encode() + b"\n")

    def _handle(self, req: dict) -> dict:
        op = req.get("op")
        rid = req.get("id")
        now = self._now_ms()
        if op == "hello":
            return {"id": rid, "ok": True, "fw": SIM_FW, "proto": PROTO_VERSION,
                    "board": SIM_BOARD, "name": SIM_NAME, "ver": SIM_VER,
                    "built": "simulated", "mods": SIM_MODS, "caps": []}
        if op == "schema":
            return {"id": rid, "ok": True, "params": self._profile["params"],
                    "tlm": self._profile["tlm"]}
        if op == "getall":
            return {"id": rid, "ok": True, "vals": self._model.values()}
        if op == "get":
            key = req.get("key")
            if self._model.spec(key) is None:
                return {"id": rid, "ok": False, "err": "nokey"}
            return {"id": rid, "ok": True, "key": key, "val": self._model.get(key)}
        if op == "set":
            key, val = req.get("key"), req.get("val")
            spec = self._model.spec(key)
            if spec is None:
                return {"id": rid, "ok": False, "err": "nokey"}
            err = _validate(spec, val)
            if err:
                return {"id": rid, "ok": False, "err": err}
            self._model.set(key, val, now)
            return {"id": rid, "ok": True}
        if op == "save":
            self._model.save()
            return {"id": rid, "ok": True}
        if op == "defaults":
            self._model.load_defaults(now)
            return {"id": rid, "ok": True}
        if op == "revert":
            return {"id": rid, "ok": True, "src": self._model.revert(now)}
        if op == "tlm":
            self._tlm_on = bool(req.get("on", False))
            return {"id": rid, "ok": True}
        if op == "dfu":
            return {"id": rid, "ok": False, "err": "nodfu"}
        if op == "wifiscan":
            return {"id": rid, "ok": False, "err": "nowifi"}
        return {"id": rid, "ok": False, "err": "badop"}

    def _tlm_loop(self) -> None:
        while not self._stop.is_set():
            with self._lock:
                rate = max(1, self._model.num("tlm.rate"))
                frame = self._model.telemetry(self._now_ms()) if self._tlm_on else None
            if frame is not None:
                self._emit({"tlm": frame})
            self._stop.wait(1.0 / rate)
