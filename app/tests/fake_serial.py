"""Scripted stand-in for serial.Serial.

Real hardware cannot reliably reproduce out-of-order replies, mid-request
disconnects, or garbage bytes. This can, deterministically.
"""
import json
import queue
import threading


class FakeDisconnected(Exception):
    """Raised from readline/write once the fake port is 'unplugged'."""


class FakeSerial:
    def __init__(self, responder=None, timeout=0.05):
        # responder(request_dict, emit) -> None. emit(obj_or_str) queues a line.
        self.responder = responder
        self.timeout = timeout
        self.is_open = True
        self.written = []
        self._lines = queue.Queue()
        self._gone = False
        self._lock = threading.Lock()

    # --- test-side controls -------------------------------------------------
    def emit(self, obj):
        """Queue a line for the link to read. Accepts a dict or a raw string."""
        text = obj if isinstance(obj, str) else json.dumps(obj)
        self._lines.put(text.encode() + b"\n")

    def unplug(self):
        self._gone = True
        self._lines.put(None)   # wake a blocked readline

    # --- serial.Serial surface ---------------------------------------------
    def write(self, data):
        if self._gone:
            raise FakeDisconnected("port gone")
        with self._lock:
            self.written.append(data)
        req = json.loads(data.decode())
        if self.responder:
            self.responder(req, self.emit)
        return len(data)

    def readline(self):
        if self._gone:
            raise FakeDisconnected("port gone")
        try:
            item = self._lines.get(timeout=self.timeout)
        except queue.Empty:
            return b""          # pyserial returns empty on timeout
        if item is None:
            raise FakeDisconnected("port gone")
        return item

    def close(self):
        self.is_open = False
