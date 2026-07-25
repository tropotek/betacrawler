"""JSON-lines codec. Mirrors firmware/src/core/protocol.cpp."""
import json


class ProtocolError(Exception):
    """A line could not be understood as a protocol message."""


def encode(req_id: int, op: str, **fields) -> str:
    """Build one newline-terminated request line. None-valued fields are dropped."""
    msg = {"id": req_id, "op": op}
    msg.update({k: v for k, v in fields.items() if v is not None})
    return json.dumps(msg, separators=(",", ":")) + "\n"


def decode(line: str) -> dict:
    """Parse one line into a dict, or raise ProtocolError."""
    line = line.strip()
    if not line:
        raise ProtocolError("empty line")
    try:
        obj = json.loads(line)
    except json.JSONDecodeError as exc:
        raise ProtocolError(f"bad json: {exc}") from exc
    if not isinstance(obj, dict):
        raise ProtocolError("message is not a JSON object")
    return obj


# An id means it answers a request. No id means the device volunteered it —
# that distinction is what lets telemetry interleave with request/response.
def is_response(msg: dict) -> bool:
    return "id" in msg


def is_telemetry(msg: dict) -> bool:
    return "id" not in msg and "tlm" in msg


def is_log(msg: dict) -> bool:
    return "id" not in msg and "log" in msg
