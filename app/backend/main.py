"""FastAPI surface. This is the contract an Electron port must reimplement."""
import asyncio
import logging
import tempfile
from contextlib import asynccontextmanager
from pathlib import Path

from fastapi import FastAPI, HTTPException, Request, WebSocket, WebSocketDisconnect
from fastapi.responses import JSONResponse
from fastapi.staticfiles import StaticFiles
from pydantic import BaseModel, StrictInt, StrictStr

from . import settings_ini, terminal
from .device import DeviceModel, DeviceError, ProtoMismatch
from .firmware import (
    Catalog, DfuFlasher, EsptoolFlasher, FlashBusy, FlashSession, FirmwareError,
    validate_dfu_image, validate_esp32_image)
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


class FlashBody(BaseModel):
    id: str
    port: str | None = None


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
        elif "scan" in msg:
            payload = {"type": "scan", "data": msg["scan"]}
        else:
            payload = {"type": "raw", "data": msg}
        self.publish_event(payload["type"], payload["data"])

    def publish_event(self, kind: str, data):
        """Fan out an arbitrary typed event from any thread.

        The flash worker uses this for `{"type": "flash"}` frames. Those are
        deliberately NOT sent as `log`: app.js renders `log` in the Terminal as
        `[device] …`, and flashing progress does not come from the device — it
        comes from dfu-util running on this host.
        """
        if self._loop is None:
            log.warning("broadcaster not bound to a loop yet; dropping %s", kind)
            return
        asyncio.run_coroutine_threadsafe(
            self._fanout({"type": kind, "data": data}), self._loop)

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


def create_app(device: DeviceModel | None = None,
               catalog: Catalog | None = None,
               flasher: DfuFlasher | None = None,
               esptool_flasher: EsptoolFlasher | None = None) -> FastAPI:
    device = device or DeviceModel()
    catalog = catalog or Catalog()
    flasher = flasher or DfuFlasher()
    esptool_flasher = esptool_flasher or EsptoolFlasher()
    bus = Broadcaster()
    device.subscribe(bus.publish_threadsafe)
    flash = FlashSession(flasher, on_event=lambda ev: bus.publish_event("flash", ev))
    # Uploaded images land here. One directory per app instance, so parallel
    # test runs (and two servers on one machine) cannot scribble on each
    # other's scratch file.
    upload_dir = Path(tempfile.mkdtemp(prefix="silkscreen-fw-"))
    # Last successful DFU enumeration, served while a flash holds the device.
    last_devices: list[dict] = []

    # lifespan, not the deprecated @app.on_event("startup")
    @asynccontextmanager
    async def lifespan(_app: FastAPI):
        bus.bind(asyncio.get_running_loop())
        yield
        device.disconnect()

    app = FastAPI(title="silkscreen configurator", lifespan=lifespan)

    @app.exception_handler(DeviceError)
    async def _device_error(_request, exc: DeviceError):
        status = 409 if exc.code == "disconnected" else 400
        if exc.code == "timeout":
            status = 504
        return JSONResponse(status_code=status,
                            content={"err": exc.code, "detail": str(exc)})

    @app.exception_handler(FirmwareError)
    async def _firmware_error(_request, exc: FirmwareError):
        # Same {"err", "detail"} shape as every other error in docs/api.md.
        status = 409 if isinstance(exc, FlashBusy) else 400
        code = "busy" if isinstance(exc, FlashBusy) else "firmware"
        return JSONResponse(status_code=status,
                            content={"err": code, "detail": str(exc)})

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

    @app.post("/api/params/revert")
    def revert():
        src = device.revert()
        return {"ok": True, "src": src, "vals": device.values()}

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

    # --- firmware / DFU -----------------------------------------------------
    # Everything here works while DISCONNECTED, on purpose: a board that needs
    # re-flashing is frequently a board that cannot be talked to. The one
    # exception is enter-dfu, which by definition needs a live device.

    def _release_if_connected_on(port: str) -> None:
        # Only ESP32/esptool flashing needs this: DFU's board disappears
        # from its serial port on its own (see enter_dfu()) before a DFU
        # flash ever starts, so the app is never holding the port DfuFlasher
        # needs. An ESP32 stays on the same port throughout, so if the app
        # happens to be connected to the very board being flashed, esptool
        # can't also open it -- release it first. A board connected on a
        # DIFFERENT port is left alone.
        if device.status().get("port") == port:
            device.disconnect()

    @app.get("/api/firmware/catalog")
    def firmware_catalog():
        """What this app shipped with, plus which entry suits the last board seen.

        A board in DFU mode reports `0483:df11` and nothing else — it cannot
        say what it is. So `recommended` is derived from the `board` string of
        the last successful `hello`, and is null when no board has been
        connected in this session. The UI says so rather than guessing.
        """
        board = device.status().get("board")
        images = catalog.images()
        recommended = next(
            (img["id"] for img in images
             if board and img.get("board") == board and img.get("available")),
            None)
        return {"app_version": catalog.app_version(),
                "images": images,
                "board": board,
                "recommended": recommended}

    @app.get("/api/firmware/dfu-status")
    def firmware_dfu_status():
        # Never enumerate while a flash is running. `dfu-util -l` OPENS the USB
        # device, and the UI polls this every 1.5s -- so a long download would
        # be interrupted repeatedly by a second process claiming the same
        # interface mid-write. Serving the last known list keeps the UI honest
        # (the board is, after all, still in DFU) and keeps exactly one process
        # touching the device at a time.
        if flash.busy:
            return {"present": True, "devices": last_devices, "busy": True}
        last_devices[:] = flasher.devices()
        return {"present": bool(last_devices), "devices": last_devices,
                "busy": False}

    @app.post("/api/firmware/enter-dfu")
    def firmware_enter_dfu():
        if device.status()["state"] != "connected":
            raise DeviceError("disconnected", "not connected")
        device.enter_dfu()
        # The port is gone by now (see DeviceModel.enter_dfu), so this reports
        # the post-reboot state rather than the one the caller started in.
        return {"ok": True, **device.status()}

    # --- wifi -----------------------------------------------------------------

    @app.post("/api/wifi/scan")
    def wifi_scan():
        if device.status()["state"] != "connected":
            raise DeviceError("disconnected", "not connected")
        device.start_wifi_scan()
        return {"ok": True}

    @app.post("/api/firmware/flash")
    def firmware_flash(body: FlashBody):
        # verify() re-hashes the file on disk instead of trusting the
        # manifest: the two can drift (a bad merge, a partial checkout), and
        # the cost of being wrong is a board that no longer boots.
        path = catalog.verify(body.id)
        img = catalog.get(body.id)
        label = f"{img['name']} {img['version']} ({img['board']})"
        if img.get("method") == "esptool":
            if not body.port:
                raise FirmwareError("a port is required to flash this image")
            _release_if_connected_on(body.port)
            flash.start(path, label, flasher=esptool_flasher, wait=False,
                       port=body.port)
        else:
            flash.start(path, label)
        return {"ok": True, "id": body.id}

    @app.post("/api/firmware/flash-upload")
    async def firmware_flash_upload(request: Request, filename: str = "uploaded image"):
        """The Advanced path: flash a .bin the user picked themselves.

        Takes the image as the raw request body rather than a multipart form.
        That avoids a `python-multipart` dependency for a single endpoint, and
        the caller only has to produce bytes — no FormData wrapper, and no
        browser-only type in the app's transport seam (see `Api.flashUpload`).

        Unlike a bundled image there is no checksum to check this against, so
        validate_image() is the only thing standing between "picked the wrong
        file out of .pio/build" and a board that no longer enumerates.
        """
        blob = await request.body()
        validate_image(blob)
        path = upload_dir / "upload.bin"
        path.write_bytes(blob)
        flash.start(path, filename)
        return {"ok": True, "filename": filename, "size": len(blob)}

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
            "dirty": result.dirty,
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
