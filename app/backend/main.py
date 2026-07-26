"""FastAPI surface. This is the contract an Electron port must reimplement."""
import asyncio
import logging
from contextlib import asynccontextmanager
from pathlib import Path

from fastapi import FastAPI, HTTPException, WebSocket, WebSocketDisconnect
from fastapi.responses import JSONResponse
from fastapi.staticfiles import StaticFiles
from pydantic import BaseModel, StrictInt, StrictStr

from . import settings_ini, terminal
from .device import DeviceModel, DeviceError, ProtoMismatch
from .link import list_candidate_ports

log = logging.getLogger(__name__)

WEB_DIR = Path(__file__).resolve().parent.parent / "web"


class ConnectBody(BaseModel):
    port: str


class ValueBody(BaseModel):
    # Strict variants: pydantic's plain `int | str` coerces `true` -> 1 and
    # `5.0` -> 5, which contradicts the project's "reject, never coerce"
    # rule and defeats device.py's own isinstance(val, bool) guard for
    # anything going through this HTTP route.
    val: StrictInt | StrictStr


class TerminalBody(BaseModel):
    command: str


class RestoreBody(BaseModel):
    ini: str


class Broadcaster:
    """Fans messages from the serial reader thread out to WebSocket clients.

    `publish_threadsafe` is invoked from SerialLink's reader thread (a plain
    `threading.Thread`, not a coroutine) whenever the device volunteers a
    message with no request id — telemetry, logs, or unsolicited state
    changes. That thread must never touch the asyncio event loop or any
    WebSocket directly; `asyncio.run_coroutine_threadsafe` is the one safe way
    to hand work from a foreign thread into a running loop. The loop
    reference is captured once, in `bind()`, from inside the FastAPI lifespan
    (i.e. after `asyncio.get_running_loop()` actually has something to
    return) — capturing it any earlier, or re-deriving it per-call, is how
    this class of bug goes silently missing: `run_coroutine_threadsafe`
    against a `None` or stale loop doesn't raise, it just never schedules the
    coroutine.
    """

    def __init__(self):
        self._clients: set[WebSocket] = set()
        self._loop: asyncio.AbstractEventLoop | None = None

    def bind(self, loop):
        self._loop = loop

    def add(self, ws):
        self._clients.add(ws)

    def remove(self, ws):
        self._clients.discard(ws)

    def publish_threadsafe(self, msg: dict):
        if self._loop is None:
            log.warning("broadcaster not bound to a loop yet; dropping %r", msg)
            return
        if "tlm" in msg:
            payload = {"type": "tlm", "data": msg["tlm"]}
        elif "log" in msg:
            payload = {"type": "log", "data": msg["log"]}
        elif "state" in msg:
            payload = {"type": "state", "data": msg["state"]}
        else:
            payload = {"type": "raw", "data": msg}
        asyncio.run_coroutine_threadsafe(self._fanout(payload), self._loop)

    async def _fanout(self, payload: dict):
        for ws in list(self._clients):
            try:
                await ws.send_json(payload)
            except Exception:
                # Almost always just a client that disconnected mid-send.
                # Logged at debug (with traceback) rather than silently
                # discarded so a genuine serialization bug in `payload`
                # doesn't look identical to "client went away".
                log.debug("dropping ws client %r", ws, exc_info=True)
                self._clients.discard(ws)


def create_app(device: DeviceModel | None = None) -> FastAPI:
    device = device or DeviceModel()
    bus = Broadcaster()
    device.subscribe(bus.publish_threadsafe)

    # lifespan, not the deprecated @app.on_event("startup")
    @asynccontextmanager
    async def lifespan(_app: FastAPI):
        bus.bind(asyncio.get_running_loop())
        yield
        device.disconnect()

    app = FastAPI(title="app-demo configurator", lifespan=lifespan)

    @app.exception_handler(DeviceError)
    async def _device_error(_request, exc: DeviceError):
        status = 409 if exc.code == "disconnected" else 400
        if exc.code == "timeout":
            status = 504
        return JSONResponse(status_code=status,
                            content={"err": exc.code, "detail": str(exc)})

    @app.get("/api/ports")
    def ports():
        return list_candidate_ports()

    @app.post("/api/connect")
    def connect(body: ConnectBody):
        try:
            device.connect(body.port)
        except ProtoMismatch as exc:
            raise HTTPException(status_code=502, detail=str(exc)) from exc
        except DeviceError as exc:
            # Covers both connect_failed (bad port) and timeout (handshake
            # never answered) — neither is a client input error, so this is
            # handled here rather than falling through to the DeviceError
            # exception_handler above (which is tuned for post-connect
            # operations like set/save where 400/409/504 make sense).
            raise HTTPException(status_code=502, detail=str(exc)) from exc
        return device.status()

    @app.post("/api/disconnect")
    def disconnect():
        device.disconnect()
        return device.status()

    @app.get("/api/status")
    def status():
        return device.status()

    @app.get("/api/schema")
    def schema():
        return device.schema()

    @app.get("/api/params")
    def params():
        return device.values()

    @app.put("/api/params/{key}")
    def set_param(key: str, body: ValueBody):
        # DeviceModel.set() checks the cached schema (by key) before it
        # checks the link's connection state, so a never-connected device
        # (empty schema cache) would otherwise surface "nokey" (400) instead
        # of "disconnected" (409) for every key. Gate on connection state
        # here so callers get a consistent 409 regardless of whether the key
        # is one this device happens to remember.
        if device.status()["state"] != "connected":
            raise DeviceError("disconnected", "not connected")
        device.set(key, body.val)
        return {"ok": True, "key": key, "val": body.val}

    @app.post("/api/params/save")
    def save():
        device.save()
        return {"ok": True}

    @app.post("/api/params/defaults")
    def defaults():
        device.load_defaults()
        return {"ok": True, "vals": device.values()}

    @app.post("/api/params/restore")
    def restore(body: RestoreBody):
        """Apply a settings dump produced by the Terminal's `dump` command.

        Deliberately partial-tolerant: a key this firmware doesn't have (a
        dump taken from a board with more modules enabled) or a value it
        rejects is reported in `skipped`, and every other key still applies.
        Aborting on the first bad line would make a restore all-or-nothing
        across firmware versions, which is exactly when you need it most.

        Applies to RAM only -- persisting stays an explicit Save, the same
        rule the config form follows.
        """
        if device.status()["state"] != "connected":
            raise DeviceError("disconnected", "not connected")
        try:
            pairs = settings_ini.parse_ini(
                body.ini, known_keys=[p["key"] for p in device.schema()["params"]])
        except ValueError as exc:
            # DeviceError rather than HTTPException so this comes back in the
            # same {"err", "detail"} shape as every other 400 in docs/api.md
            # (HTTPException would nest it under "detail").
            raise DeviceError("badini", str(exc)) from exc

        applied, skipped = [], []
        for key, raw in pairs:
            try:
                device.terminal_set(key, raw)
                applied.append(key)
            except DeviceError as exc:
                skipped.append({"key": key, "reason": str(exc)})
        return {"ok": bool(applied) and not skipped,
                "applied": applied, "skipped": skipped, "vals": device.values()}

    @app.post("/api/terminal")
    def terminal_command(body: TerminalBody):
        # Always 200: terminal.run() never raises, so command-level failures
        # (unknown key, bad value, disconnected, ...) are carried in the
        # `ok`/`friendly` fields shell-REPL style, not as an HTTP error status.
        result = terminal.run(device, body.command)
        return {
            "ok": result.ok,
            "friendly": result.friendly,
            "raw_sent": result.raw_sent,
            "raw_recv": result.raw_recv,
        }

    @app.websocket("/ws")
    async def ws_endpoint(ws: WebSocket):
        await ws.accept()
        bus.add(ws)
        await ws.send_json({"type": "state", "data": device.status()})
        try:
            while True:
                await ws.receive_text()   # clients send nothing; this detects close
        except WebSocketDisconnect:
            pass
        finally:
            bus.remove(ws)

    if WEB_DIR.is_dir():
        app.mount("/", StaticFiles(directory=str(WEB_DIR), html=True), name="web")

    return app


app = create_app()
