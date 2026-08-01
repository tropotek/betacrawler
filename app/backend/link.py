"""Owns the serial port in a dedicated thread.

pyserial's read is blocking. Calling it from the asyncio event loop freezes the
whole server and looks exactly like a board crash, so all port I/O lives here.
"""
import threading
import logging

import serial
from serial.tools import list_ports

from . import protocol

log = logging.getLogger(__name__)

# USB VID:PID pairs for the boards this app knows how to talk to, in the
# order the port dropdown should prefer them when auto-selecting. A CP2102
# is a generic USB-UART bridge chip (used by plenty of things that aren't
# this project's ESP32 board), not a project-specific ID the way the
# STM32's own VID:PID is -- but for this template, matching on it is the
# practical choice: a wrong guess only pre-selects the wrong port in a
# dropdown the user can still freely override, never anything that
# auto-connects on its own.
_KNOWN_BOARDS = [
    (0x0483, 0x5740, "STM32"),   # Black Pill's native USB CDC
    (0x10C4, 0xEA60, "ESP32"),   # CP2102 bridge, e.g. esp32_wroom32's devkit
]


class NotConnected(Exception):
    pass


class RequestTimeout(Exception):
    pass


class _Pending:
    __slots__ = ("event", "response", "raw")

    def __init__(self):
        self.event = threading.Event()
        self.response = None
        self.raw = None


def list_candidate_ports() -> list[dict]:
    out = []
    for p in list_ports.comports():
        board = next(
            (name for vid, pid, name in _KNOWN_BOARDS if p.vid == vid and p.pid == pid),
            None,
        )
        out.append({
            "port": p.device,
            "desc": p.description,
            "vid": f"{p.vid:04x}" if p.vid else None,
            "pid": f"{p.pid:04x}" if p.pid else None,
            "match": board is not None,
            "board": board,
        })
    return out


def _default_open(port: str):
    return serial.Serial(port, baudrate=115200, timeout=0.2)


class SerialLink:
    def __init__(self, open_port=None):
        self._open_port = open_port or _default_open
        self._port = None
        self._reader = None
        self._stop = threading.Event()
        self._pending: dict[int, _Pending] = {}
        self._lock = threading.Lock()
        self._write_lock = threading.Lock()
        self._next_id = 1
        self._subs = []
        self._state = "disconnected"

    @property
    def state(self) -> str:
        return self._state

    def subscribe(self, callback):
        self._subs.append(callback)

    def connect(self, port: str):
        self.disconnect()
        self._port = self._open_port(port)
        self._stop.clear()
        self._state = "connected"
        self._reader = threading.Thread(target=self._read_loop, daemon=True)
        self._reader.start()

    def disconnect(self):
        self._stop.set()
        port, self._port = self._port, None
        if port is not None:
            try:
                port.close()
            except Exception:
                pass
        if self._reader and self._reader.is_alive():
            self._reader.join(timeout=1.0)
        self._reader = None
        self._state = "disconnected"
        self._fail_all_pending()

    def request(self, op: str, timeout: float = 1.0, **fields) -> dict:
        return self._do_request(op, timeout, **fields)[2]

    def request_raw(self, op: str, timeout: float = 1.0, **fields) -> tuple[str, str, dict]:
        """Like request(), but also returns the exact wire lines sent/received.

        Used by DeviceModel's terminal_* methods to power the debug Terminal
        page's "show raw JSON" toggle; request() itself doesn't need this and
        keeps its original signature/contract.
        """
        return self._do_request(op, timeout, **fields)

    def _do_request(self, op: str, timeout: float = 1.0, **fields) -> tuple[str, str, dict]:
        port = self._port
        if port is None or self._state != "connected":
            raise NotConnected("no serial connection")

        with self._lock:
            req_id = self._next_id
            self._next_id = self._next_id % 65535 + 1
            slot = _Pending()
            self._pending[req_id] = slot

        line = protocol.encode(req_id, op, **fields)
        sent = line.rstrip("\n")
        try:
            with self._write_lock:
                port.write(line.encode())
        except Exception as exc:
            self._drop_pending(req_id)
            self._on_port_lost()
            raise NotConnected(f"write failed: {exc}") from exc

        if not slot.event.wait(timeout):
            self._drop_pending(req_id)
            raise RequestTimeout(f"no response to {op} within {timeout}s")

        self._drop_pending(req_id)
        if slot.response is None:
            raise NotConnected("connection lost while waiting")
        return sent, slot.raw or "", slot.response

    # --- internals ----------------------------------------------------------
    def _drop_pending(self, req_id: int):
        with self._lock:
            self._pending.pop(req_id, None)

    def _fail_all_pending(self):
        with self._lock:
            waiters = list(self._pending.values())
            self._pending.clear()
        for slot in waiters:
            slot.response = None
            slot.event.set()

    def _on_port_lost(self):
        self._state = "disconnected"
        self._stop.set()
        self._fail_all_pending()
        self._publish({"state": "disconnected"})

    def _publish(self, msg: dict):
        for cb in list(self._subs):
            try:
                cb(msg)
            except Exception:
                log.exception("subscriber raised")

    def _read_loop(self):
        port = self._port
        while not self._stop.is_set():
            try:
                raw = port.readline()
            except Exception:
                self._on_port_lost()
                return
            if not raw:
                continue
            try:
                msg = protocol.decode(raw.decode(errors="replace"))
            except protocol.ProtocolError:
                log.debug("discarding unparseable line: %r", raw)
                continue      # one bad line must never wedge the reader

            if protocol.is_response(msg):
                with self._lock:
                    slot = self._pending.get(msg["id"])
                if slot is not None:
                    slot.response = msg
                    slot.raw = raw.decode(errors="replace").strip()
                    slot.event.set()
                else:
                    log.debug("response for unknown id %s", msg.get("id"))
            else:
                self._publish(msg)
