import pytest
from backend.protocol import (
    ProtocolError, encode, decode, is_response, is_telemetry, is_log,
)


def test_encode_produces_one_terminated_line():
    line = encode(7, "set", key="rx.deadband_us", val=5)
    assert line.endswith("\n")
    assert line.count("\n") == 1
    assert decode(line) == {"id": 7, "op": "set", "key": "rx.deadband_us", "val": 5}


def test_encode_omits_none_fields():
    assert "key" not in decode(encode(1, "hello", key=None))


def test_decode_rejects_garbage():
    with pytest.raises(ProtocolError):
        decode("{not json")
    with pytest.raises(ProtocolError):
        decode("")
    with pytest.raises(ProtocolError):
        decode("[1,2,3]")          # valid JSON, wrong shape


def test_message_classification():
    resp = {"id": 3, "ok": True}
    tlm = {"tlm": {"up": 10}}
    log = {"log": "saved"}

    assert is_response(resp) and not is_telemetry(resp) and not is_log(resp)
    assert is_telemetry(tlm) and not is_response(tlm)
    assert is_log(log) and not is_response(log)
