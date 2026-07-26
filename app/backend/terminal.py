"""Parses text typed into the debug Terminal page into device operations.

Deliberately routes everything through DeviceModel's schema-validated
terminal_* methods rather than a raw byte passthrough -- there is no generic
passthrough op on the wire (firmware/src/core/protocol.cpp's opFromString
only knows hello|schema|get|getall|set|save|defaults|tlm), and parsing here
instead of in JS is what makes this unit-testable against the fake serial
port (see tests/test_terminal.py).
"""
from dataclasses import dataclass

from . import settings_ini
from .device import DeviceModel, DeviceError

HELP_TEXT = "\n".join([
    "Commands:",
    "  get <key>            read a parameter's current value",
    "  set <key> <value>    set a parameter",
    "  save                 commit current values to flash",
    "  defaults             reset all values to firmware defaults",
    "  list                 list every setting with its valid values",
    "  dump                 print all settings as INI text (copy it to a file;",
    "                       'Restore from INI' feeds it back)",
    "  help                 show this text",
])


@dataclass
class TerminalResult:
    ok: bool
    friendly: str
    raw_sent: str = ""
    raw_recv: str = ""


def _valid_values(spec: dict) -> str:
    """The human form of a parameter's constraint, e.g. "1..20"."""
    kind = spec["type"]
    if kind == "u8":
        return f"{spec['min']}..{spec['max']}"
    if kind == "enum":
        return "|".join(spec["options"])
    if kind == "str":
        return f"max {spec['maxlen']} chars"
    return ""


def _list_text(schema: list[dict]) -> str:
    """Render the schema as an aligned, group-headed settings reference.

    Everything here comes out of the descriptor the firmware published, so a
    board that enables another module documents itself -- there is deliberately
    no per-key text in this file.
    """
    if not schema:
        return "No settings — is a device connected?"

    rows = []
    for spec in schema:
        label = spec.get("label") or spec["key"]
        unit = spec.get("unit")
        rows.append({
            "group": spec.get("group") or "",
            "cells": [spec["key"],
                      f"{label} ({unit})" if unit else label,
                      spec["type"],
                      _valid_values(spec),
                      f"(default: {spec.get('def')})"],
        })
    widths = [max(len(r["cells"][i]) for r in rows) for i in range(len(rows[0]["cells"]))]

    # Grouped by first appearance rather than straight down the schema, so a
    # schema interleaving two modules' keys still prints one heading each.
    order, by_group = [], {}
    for row in rows:
        if row["group"] not in by_group:
            by_group[row["group"]] = []
            order.append(row["group"])
        by_group[row["group"]].append(row["cells"])

    lines = ["Settings — change one with: set <key> <value>"]
    for group in order:
        lines.append("")
        lines.append(group)
        for cells in by_group[group]:
            padded = "  ".join(c.ljust(w) for c, w in zip(cells, widths))
            lines.append(f"  {padded}".rstrip())
    return "\n".join(lines)


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

        if cmd == "list":
            if args:
                return TerminalResult(False, "ERROR: usage: list")
            return TerminalResult(True, _list_text(device.schema()["params"]))

        if cmd == "dump":
            if args:
                return TerminalResult(False, "ERROR: usage: dump")
            sent, recv, vals = device.terminal_getall()
            text = settings_ini.dump_ini(
                device.schema()["params"], vals, device.status())
            return TerminalResult(True, text, sent, recv)

        return TerminalResult(False, f"ERROR: unknown command {cmd!r}. Type 'help' for a list.")
    except DeviceError as exc:
        return TerminalResult(False, f"ERROR: {exc}")
