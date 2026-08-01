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


def _revert_error(resp: dict) -> tuple[str, str]:
    """(code, message) for a refused `revert`.

    Shared by revert() and terminal_revert() so the Terminal and the button
    cannot drift apart on the one case a user is actually likely to hit.

    `badop` here means the board predates the op entirely — the same shape
    enter_dfu() already handles, and a state made likely by the fact that the
    app ships the firmware that matches it. "revert failed" would send someone
    hunting a flash fault; the real fix is one page away.
    """
    err = resp.get("err", "err")
    if err == "badop":
        return ("badop",
                "this firmware is too old to know `revert`. Update the "
                "firmware from the Firmware page.")
    return (err, "revert failed")


class DeviceModel:
    def __init__(self, link: SerialLink | None = None):
        self._link = link or SerialLink()
        self._schema: list[dict] = []
        self._tlm_schema: list[dict] = []
        self._by_key: dict[str, dict] = {}
        self._values: dict = {}
        self._info: dict = {}
        self._port: str | None = None

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
            # `fw` stays the display string; name/ver/built/mods are the
            # structured fields the modular firmware added beside it. Older
            # firmware simply omits them and they come through as None.
            self._info = {
                "fw": hello.get("fw"),
                "proto": hello.get("proto"),
                "board": hello.get("board"),
                "name": hello.get("name"),
                "ver": hello.get("ver"),
                "built": hello.get("built"),
                "mods": hello.get("mods", []),
                # Optional device capabilities, e.g. ["dfu"]. Firmware built
                # without FEATURE_DFU -- and any firmware predating `caps`
                # entirely -- reports nothing, which is what lets the UI grey
                # out "Reboot to DFU" instead of offering a button that
                # answers `nodfu`.
                "caps": hello.get("caps", []),
            }
            schema = self._send("schema")
            self._schema = schema["params"]
            # Descriptor for the telemetry fields this build publishes. The
            # firmware's module set decides it, so the UI needs no per-field
            # knowledge; a board with an extra sensor just gets an extra card.
            self._tlm_schema = schema.get("tlm", [])
            self._by_key = {p["key"]: p for p in self._schema}
            self._values = self._send("getall")["vals"]
            self._port = port
        except Exception:
            self._link.disconnect()
            self._schema, self._tlm_schema = [], []
            self._by_key, self._values, self._info = {}, {}, {}
            self._port = None
            raise

    def disconnect(self):
        self._link.disconnect()

    # --- reads --------------------------------------------------------------
    def status(self) -> dict:
        return {"state": self._link.state, "port": self._port, **self._info}

    def schema(self) -> dict:
        """Both descriptors the UI renders from: config controls and telemetry
        cards. One round trip, one cache, because the firmware sends them in
        one `schema` response."""
        return {"params": self._schema, "tlm": self._tlm_schema}

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

    def revert(self):
        """Reload the settings last written to the device's flash.

        Returns which source the firmware used: "flash" normally, or
        "defaults" when the board has nothing valid stored -- a fresh board,
        or settings discarded because the enabled module set changed the
        fingerprint. The caller needs to know, because only the "flash" case
        leaves the device with nothing unsaved.
        """
        resp = self._send("revert")
        if not resp.get("ok"):
            raise DeviceError(*_revert_error(resp))
        self._values = self._send("getall")["vals"]
        # A missing `src` means firmware we don't recognise (protocol skew).
        # Default to "defaults", the direction that prompts a save, rather
        # than "flash", which would silently hide unsaved state.
        return resp.get("src", "defaults")

    def enter_dfu(self):
        """Ask the device to reboot into its ROM bootloader.

        The firmware answers BEFORE resetting (it flushes the response, waits
        briefly, then resets) — so an `ok` here means "request accepted", and
        the port disappearing a moment later is the expected outcome rather
        than a fault. Without that ordering there would be no way to tell a
        successful reboot from a board that simply died.

        Disconnecting immediately is deliberate: the port is about to become
        invalid, and letting the reader thread discover that as an I/O error
        would surface a spurious "connection lost" to the user at the exact
        moment things are working correctly.
        """
        resp = self._send("dfu", timeout=2.0)
        if not resp.get("ok"):
            err = resp.get("err", "err")
            # `nodfu` is FEATURE_DFU turned off; `badop` is firmware old enough
            # not to know the op at all. Different causes, identical meaning to
            # whoever is looking at the button, and the same fix -- so they get
            # the same message rather than leaking "badop" into the UI.
            if err in ("nodfu", "badop"):
                raise DeviceError(
                    "nodfu",
                    "this firmware does not support rebooting to DFU. Use the "
                    "BOOT0 button method instead.")
            raise DeviceError(err, "could not enter DFU mode")
        self._link.disconnect()

    def start_wifi_scan(self):
        """Arm an SSID scan. Results arrive later as a `scan` WS push --
        see main.py's Broadcaster -- not in this response, which only
        confirms the firmware started scanning.
        """
        resp = self._send("wifiscan", timeout=2.0)
        if not resp.get("ok"):
            err = resp.get("err", "err")
            if err in ("nowifi", "badop"):
                raise DeviceError(
                    "nowifi",
                    "this firmware does not support scanning for networks.")
            raise DeviceError(err, "could not start a WiFi scan")

    # --- terminal (debug page) -----------------------------------------------
    # Mirrors set/save/load_defaults above but also returns the exact wire
    # lines exchanged, for the Terminal page's "show raw JSON" toggle. Kept as
    # separate methods rather than a `raw` flag on the existing ones so the
    # well-tested public contract of set()/save()/load_defaults() never has
    # to change shape based on a flag.
    def terminal_get(self, key: str):
        if key not in self._by_key:
            raise DeviceError("nokey", f"unknown parameter {key!r}")
        sent, recv, resp = self._send_raw("get", key=key)
        if not resp.get("ok"):
            raise DeviceError(resp.get("err", "err"), f"device rejected get {key}")
        val = resp["val"]
        self._values[key] = val
        return sent, recv, val

    def terminal_getall(self):
        """Re-read every value from the device and refresh the cache.

        `dump` uses this rather than values(): a settings backup should be the
        device's own answer, not whatever this process last happened to see,
        and it gives the raw-JSON toggle a real exchange to display.
        """
        sent, recv, resp = self._send_raw("getall")
        if not resp.get("ok"):
            raise DeviceError(resp.get("err", "err"), "getall failed")
        self._values = resp["vals"]
        return sent, recv, dict(self._values)

    def terminal_set(self, key: str, raw_value: str):
        spec = self._by_key.get(key)
        if spec is None:
            raise DeviceError("nokey", f"unknown parameter {key!r}")
        if spec["type"] == "u8":
            try:
                val = int(raw_value)
            except ValueError:
                raise DeviceError("badtype", "expected an integer") from None
        else:
            val = raw_value

        self._validate(spec, val)

        sent, recv, resp = self._send_raw("set", key=key, val=val)
        if not resp.get("ok"):
            raise DeviceError(resp.get("err", "err"), f"device rejected {key}")
        self._values[key] = val
        return sent, recv, val

    def terminal_save(self):
        sent, recv, resp = self._send_raw("save", timeout=5.0)
        if not resp.get("ok"):
            raise DeviceError(resp.get("err", "err"), "save failed")
        return sent, recv

    def terminal_defaults(self):
        sent, recv, resp = self._send_raw("defaults")
        if not resp.get("ok"):
            raise DeviceError(resp.get("err", "err"), "defaults failed")
        self._values = self._send("getall")["vals"]
        return sent, recv

    def terminal_revert(self):
        sent, recv, resp = self._send_raw("revert")
        if not resp.get("ok"):
            raise DeviceError(*_revert_error(resp))
        self._values = self._send("getall")["vals"]
        # See revert() above: an absent `src` defaults to "defaults" so a
        # save prompt is the fail-safe direction, not silently-hidden state.
        return sent, recv, resp.get("src", "defaults")

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
        return self._send_raw(op, timeout, **fields)[2]

    def _send_raw(self, op: str, timeout: float = 1.0, **fields):
        try:
            return self._link.request_raw(op, timeout=timeout, **fields)
        except RequestTimeout as exc:
            raise DeviceError("timeout", str(exc)) from exc
        except NotConnected as exc:
            raise DeviceError("disconnected", str(exc)) from exc
