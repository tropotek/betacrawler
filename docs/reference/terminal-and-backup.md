# Terminal and backup

The **Terminal** page is direct access to the board. Anything the curated pages can do, you can do
here by name — including settings that no page happens to show.

## Commands

| Command | What it does |
|---|---|
| `help` | List the commands. The only one that works while disconnected. |
| `list` | List every parameter the board publishes, with its valid values. |
| `get <key>` | Read one setting. |
| `set <key> <value>` | Change one setting. Applies immediately, in RAM. |
| `save` | Write the current settings to flash. |
| `revert` | Reload settings from flash, discarding unsaved changes. |
| `defaults` | Reset everything to factory defaults. Not saved until you `save`. |
| `dump` | Print all current settings as an INI file. |

A worked example:

```
> get rx.protocol
rx.protocol = elrs

> set tank_drive.forward_ratio 70
OK: tank_drive.forward_ratio = 70

> save
OK: saved to flash
```

If you `set` something and change your mind, `revert` puts you back to what is in flash. On a
board that has never been saved, `revert` loads the defaults instead and tells you so.

## Seeing the raw traffic

Two toggles at the top of the page:

- **Show raw JSON** — the messages the app exchanges with the board.
- **Show device traffic** — everything on the wire, including telemetry.

Useful when something is not behaving and you want to see what the board actually said, rather
than how the app chose to display it.

## Backing up settings

`dump` prints your entire configuration as INI text. Copy it somewhere safe and you have a
restore point — worth doing once you have a setup you like.

The **Restore from INI…** button on the Terminal page reads such a file back. Values are applied
one at a time, exactly as if you had typed a `set` for each, so anything invalid is reported
rather than silently skipped.

Restoring applies to RAM. Press **Save to flash** afterwards to make it stick.

## Keys the pages do not show

The app's pages are hand-picked: each one shows the settings that belong on it, with a label and
an order chosen for that page. A setting that no page lists still exists on the board, is still
validated, and is still reachable here by its key.

`list` is how you find them.
