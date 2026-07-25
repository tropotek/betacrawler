"""Parses text typed into the debug Terminal page into device operations.

Deliberately routes everything through DeviceModel's schema-validated
terminal_* methods rather than a raw byte passthrough -- there is no generic
passthrough op on the wire (firmware/src/core/protocol.cpp's opFromString
only knows hello|schema|get|getall|set|save|defaults|tlm), and parsing here
instead of in JS is what makes this unit-testable against the fake serial
port (see tests/test_terminal.py).
"""
from dataclasses import dataclass

from .device import DeviceModel, DeviceError

HELP_TEXT = "\n".join([
    "Commands:",
    "  get <key>            read a parameter's current value",
    "  set <key> <value>    set a parameter",
    "  save                 commit current values to flash",
    "  defaults             reset all values to firmware defaults",
    "  help                 show this text",
])


@dataclass
class TerminalResult:
    ok: bool
    friendly: str
    raw_sent: str = ""
    raw_recv: str = ""


def run(device: DeviceModel, command: str) -> TerminalResult:
    parts = command.split()
    if not parts:
        return TerminalResult(False, "ERROR: empty command. Type 'help' for a list.")
    cmd, *args = parts

    try:
        if cmd == "help":
            if args:
                return TerminalResult(False, "ERROR: usage: help")
            return TerminalResult(True, HELP_TEXT)

        if cmd == "get":
            if len(args) != 1:
                return TerminalResult(False, "ERROR: usage: get <key>")
            sent, recv, val = device.terminal_get(args[0])
            return TerminalResult(True, f"{args[0]} = {val}", sent, recv)

        if cmd == "set":
            if len(args) != 2:
                return TerminalResult(False, "ERROR: usage: set <key> <value>")
            key, raw_val = args
            sent, recv, val = device.terminal_set(key, raw_val)
            return TerminalResult(True, f"OK: {key} = {val}", sent, recv)

        if cmd == "save":
            if args:
                return TerminalResult(False, "ERROR: usage: save")
            sent, recv = device.terminal_save()
            return TerminalResult(True, "OK: saved to flash", sent, recv)

        if cmd == "defaults":
            if args:
                return TerminalResult(False, "ERROR: usage: defaults")
            sent, recv = device.terminal_defaults()
            return TerminalResult(True, "OK: reset to defaults", sent, recv)

        return TerminalResult(False, f"ERROR: unknown command {cmd!r}. Type 'help' for a list.")
    except DeviceError as exc:
        return TerminalResult(False, f"ERROR: {exc}")
