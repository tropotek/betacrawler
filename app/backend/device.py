"""Device model: schema cache, value cache, connection state.

Validates against the cached schema before sending, so bad input fails fast
with a useful message. The firmware validates again regardless — it must never
trust its host.
"""
import logging

from .link import SerialLink, NotConnected, RequestTimeout

log = logging.getLogger(__name__)

PROTO_VERSION = 1


class ProtoMismatch(Exception):
    pass


class DeviceError(Exception):
    def __init__(self, code: str, message: str = ""):
        super().__init__(message or code)
        self.code = code


class DeviceModel:
    def __init__(self, link: SerialLink | None = None):
        self._link = link or SerialLink()
        self._schema: list[dict] = []
        self._by_key: dict[str, dict] = {}
        self._values: dict = {}
        self._info: dict = {}

    def subscribe(self, callback):
        self._link.subscribe(callback)

    # --- lifecycle ----------------------------------------------------------
    def connect(self, port: str):
        try:
            self._link.connect(port)
        except Exception as exc:
            raise DeviceError("connect_failed", str(exc)) from exc

        try:
            hello = self._send("hello")
            if hello.get("proto") != PROTO_VERSION:
                raise ProtoMismatch(
                    f"device speaks proto {hello.get('proto')}, "
                    f"this app speaks {PROTO_VERSION}"
                )
            self._info = {
                "fw": hello.get("fw"),
                "proto": hello.get("proto"),
                "board": hello.get("board"),
            }
            self._schema = self._send("schema")["params"]
            self._by_key = {p["key"]: p for p in self._schema}
            self._values = self._send("getall")["vals"]
        except Exception:
            self._link.disconnect()
            self._schema, self._by_key, self._values, self._info = [], {}, {}, {}
            raise

    def disconnect(self):
        self._link.disconnect()

    # --- reads --------------------------------------------------------------
    def status(self) -> dict:
        return {"state": self._link.state, **self._info}

    def schema(self) -> list[dict]:
        return self._schema

    def values(self) -> dict:
        return dict(self._values)

    # --- writes -------------------------------------------------------------
    def set(self, key: str, val):
        spec = self._by_key.get(key)
        if spec is None:
            raise DeviceError("nokey", f"unknown parameter {key!r}")

        self._validate(spec, val)

        resp = self._send("set", key=key, val=val)
        if not resp.get("ok"):
            raise DeviceError(resp.get("err", "err"), f"device rejected {key}")
        self._values[key] = val

    def save(self):
        # Flash erase stalls the MCU ~1s, so this needs a longer timeout than
        # a normal request.
        resp = self._send("save", timeout=5.0)
        if not resp.get("ok"):
            raise DeviceError(resp.get("err", "err"), "save failed")

    def load_defaults(self):
        resp = self._send("defaults")
        if not resp.get("ok"):
            raise DeviceError(resp.get("err", "err"), "defaults failed")
        self._values = self._send("getall")["vals"]

    # --- internals ----------------------------------------------------------
    @staticmethod
    def _validate(spec: dict, val):
        kind = spec["type"]
        if kind == "u8":
            if not isinstance(val, int) or isinstance(val, bool):
                raise DeviceError("badtype", "expected an integer")
            if val < spec["min"] or val > spec["max"]:
                raise DeviceError(
                    "range", f"must be {spec['min']}..{spec['max']}")
        elif kind == "enum":
            if val not in spec["options"]:
                raise DeviceError(
                    "enum", f"must be one of {', '.join(spec['options'])}")
        elif kind == "str":
            if not isinstance(val, str):
                raise DeviceError("badtype", "expected a string")
            if len(val) > spec["maxlen"]:
                raise DeviceError(
                    "toolong", f"max {spec['maxlen']} characters")

    def _send(self, op: str, timeout: float = 1.0, **fields) -> dict:
        try:
            return self._link.request(op, timeout=timeout, **fields)
        except RequestTimeout as exc:
            raise DeviceError("timeout", str(exc)) from exc
        except NotConnected as exc:
            raise DeviceError("disconnected", str(exc)) from exc
