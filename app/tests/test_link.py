import json
import threading
import time

import pytest

from backend.link import SerialLink, NotConnected, RequestTimeout
from tests.fake_serial import FakeSerial, FakeDisconnected


def make_link(responder=None):
    fake = FakeSerial(responder=responder)
    link = SerialLink(open_port=lambda port: fake)
    return link, fake


def echo_ok(req, emit):
    emit({"id": req["id"], "ok": True, "op": req["op"]})


def test_request_resolves_with_matching_id():
    link, _ = make_link(echo_ok)
    link.connect("/dev/fake")
    try:
        resp = link.request("hello")
        assert resp["ok"] is True
        assert resp["op"] == "hello"
    finally:
        link.disconnect()


def test_out_of_order_responses_resolve_correctly():
    """id=8 answered before id=7. Each caller must get its own reply."""
    held = []

    def responder(req, emit):
        held.append((req, emit))
        if len(held) == 2:
            (r1, e1), (r2, e2) = held
            e2({"id": r2["id"], "ok": True, "who": "second"})
            e1({"id": r1["id"], "ok": True, "who": "first"})

    link, _ = make_link(responder)
    link.connect("/dev/fake")
    results = {}

    def call(name):
        results[name] = link.request("get", key=name, timeout=2.0)

    t1 = threading.Thread(target=call, args=("first",))
    t2 = threading.Thread(target=call, args=("second",))
    t1.start(); time.sleep(0.02); t2.start()
    t1.join(3); t2.join(3)
    link.disconnect()

    assert results["first"]["who"] == "first"
    assert results["second"]["who"] == "second"


def test_concurrent_requests_do_not_interleave_on_the_wire():
    """Two overlapping request() calls must never tear/interleave each
    other's bytes on the wire.

    FakeSerial's default write() runs the responder synchronously inside a
    single Python call, so it can never expose a missing write lock — the
    whole write "looks" atomic regardless of what SerialLink does. Here we
    ask FakeSerial to dribble each write out in small chunks with real
    sleeps between them (write_delay), which approximates actual OS-level
    write latency: if SerialLink didn't serialize writers, two threads'
    chunks would visibly interleave in raw_written.
    """
    def responder(req, emit):
        emit({"id": req["id"], "ok": True})

    fake = FakeSerial(responder=responder, write_delay=0.005)
    link = SerialLink(open_port=lambda port: fake)
    link.connect("/dev/fake")
    results = {}

    def call(name):
        results[name] = link.request("get", key=name, timeout=2.0)

    t1 = threading.Thread(target=call, args=("a",))
    t2 = threading.Thread(target=call, args=("b",))
    t1.start(); t2.start()
    t1.join(3); t2.join(3)
    link.disconnect()

    assert results["a"]["ok"] is True
    assert results["b"]["ok"] is True

    # Reconstruct exactly what hit the "wire". If the two writes interleaved,
    # this will not cleanly split into two well-formed JSON lines.
    lines = [l for l in bytes(fake.raw_written).split(b"\n") if l]
    assert len(lines) == 2, f"expected 2 intact lines on the wire, got: {lines!r}"
    parsed = [json.loads(l) for l in lines]
    assert {p["id"] for p in parsed} == {parsed[0]["id"], parsed[1]["id"]}
    assert len({p["id"] for p in parsed}) == 2


def test_late_response_times_out():
    link, _ = make_link(responder=lambda req, emit: None)   # never answers
    link.connect("/dev/fake")
    try:
        with pytest.raises(RequestTimeout):
            link.request("hello", timeout=0.2)
    finally:
        link.disconnect()


def test_telemetry_interleaved_mid_request_is_published_not_matched():
    seen = []

    def responder(req, emit):
        emit({"tlm": {"up": 1}})            # arrives before the reply
        emit({"id": req["id"], "ok": True})
        emit({"tlm": {"up": 2}})            # and after

    link, _ = make_link(responder)
    link.subscribe(seen.append)
    link.connect("/dev/fake")
    try:
        assert link.request("hello")["ok"] is True
        time.sleep(0.1)
    finally:
        link.disconnect()

    tlm = [m for m in seen if "tlm" in m]
    assert len(tlm) == 2


def test_garbage_line_does_not_wedge_the_reader():
    def responder(req, emit):
        emit("}{ not json at all")          # must be discarded silently
        emit({"id": req["id"], "ok": True})

    link, _ = make_link(responder)
    link.connect("/dev/fake")
    try:
        assert link.request("hello", timeout=1.0)["ok"] is True
    finally:
        link.disconnect()


def test_port_vanishing_mid_request_disconnects_and_raises():
    holder = {}

    def unplug_on_request(req, emit):
        holder["fake"].unplug()      # board yanked before it can answer

    link, fake = make_link(unplug_on_request)
    holder["fake"] = fake

    link.connect("/dev/fake")
    with pytest.raises((RequestTimeout, NotConnected)):
        link.request("hello", timeout=1.0)
    time.sleep(0.2)
    assert link.state == "disconnected"


def test_request_without_connection_raises():
    link = SerialLink(open_port=lambda port: FakeSerial())
    with pytest.raises(NotConnected):
        link.request("hello")
