"""INI codec for whole-device settings backup and restore.

Pure text in, pure text out: no device, no serial port, no type coercion. The
Terminal's `dump` command renders with `dump_ini()`, and the Restore button's
route parses with `parse_ini()` before handing each raw string to
`DeviceModel.terminal_set()`, which already knows how to coerce and validate a
string against the schema. Keeping those two jobs apart is what lets this
module be tested with nothing attached.

Key <-> INI mapping: the part of the key before the first dot is the section,
the rest is the option -- `led.blink_hz` <-> `[led]`/`blink_hz`. A key with no
dot (none exist today) goes under `[general]`.
"""
import configparser
from datetime import datetime

GENERAL_SECTION = "general"

# configparser's real DEFAULT section leaks its options into every other
# section, which would silently invent settings on restore. Renaming it to
# something no one will type demotes a literal `[DEFAULT]` block to an
# ordinary -- and therefore harmless, and reportable -- section.
_NO_DEFAULT_SECTION = "\x00no-default"


def _split(key: str) -> tuple[str, str]:
    section, dot, option = key.partition(".")
    return (section, option) if dot else (GENERAL_SECTION, key)


def dump_ini(schema: list[dict], values: dict, info: dict) -> str:
    """Render the device's current settings as INI text.

    `schema` decides what counts as a setting and in what order (it is the
    firmware's module registration order, the same order the config form
    renders in); `values` supplies the numbers. A key in one but not the other
    is skipped, so a stale cache entry can never end up in a file that later
    gets fed back to a device.
    """
    lines = ["; silkscreen settings dump"]
    ident = " ".join(part for part in (info.get("fw"),
                                       f"({info['board']})" if info.get("board") else None)
                     if part)
    if ident:
        lines.append(f"; firmware: {ident}")
    lines.append(f"; dumped: {datetime.now().isoformat(timespec='seconds')}")
    lines.append("; restore with the Terminal page's \"Restore from INI\" button")

    # Grouped by section rather than emitted straight down the schema, so a
    # schema that interleaves two modules' keys still produces one [section]
    # header each -- a repeated header is a parse error on the way back in.
    order: list[str] = []
    by_section: dict[str, list[tuple[str, object]]] = {}
    for spec in schema:
        key = spec["key"]
        if key not in values:
            continue
        section, option = _split(key)
        if section not in by_section:
            by_section[section] = []
            order.append(section)
        by_section[section].append((option, values[key]))

    for section in order:
        lines.append("")
        lines.append(f"[{section}]")
        lines.extend(f"{option} = {value}" for option, value in by_section[section])

    return "\n".join(lines) + "\n"


def parse_ini(text: str, known_keys=()) -> list[tuple[str, str]]:
    """Parse INI text into `[(key, raw_value_string)]`, in file order.

    Values stay strings: the schema, not this parser, decides what `7` means.
    Raises `ValueError` on anything configparser refuses -- a missing section
    header, a duplicated key, a line that is not an assignment.

    `known_keys` only disambiguates the `[general]` section, where a dotless
    key and a real `general.`-prefixed key would otherwise collide. A key the
    device actually advertises wins; anything else reads as dotless.
    """
    # empty_lines_in_values=False: a settings value is one line. Without it a
    # blank line followed by an indented line reads as a continuation of the
    # previous value, so a hand-edited dump with a stray indent silently turns
    # two settings into one unparseable string.
    parser = configparser.ConfigParser(
        interpolation=None, strict=True, empty_lines_in_values=False,
        default_section=_NO_DEFAULT_SECTION)
    parser.optionxform = str          # keys are the device's, not lowercased
    try:
        parser.read_string(text)
    except configparser.Error as exc:
        raise ValueError(str(exc)) from exc

    known = set(known_keys)
    pairs = []
    for section in parser.sections():
        for option, value in parser.items(section):
            key = f"{section}.{option}"
            if section == GENERAL_SECTION and key not in known:
                key = option
            pairs.append((key, value))
    return pairs
