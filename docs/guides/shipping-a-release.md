# Shipping a Release

`app/firmware/` holds the images the in-app **Firmware** page can flash. It is gitignored build
output, not source — nothing reads it except the bundling script and the running app, and a
fresh clone starts with the folder empty.

Ship a build after any firmware change worth releasing. Name every board the release covers in
one command — that command *is* the release, and images from a previous run are pruned:

```bash
python3 app/tools/bundle_firmware.py blackpill_f411ce
python3 app/tools/bundle_firmware.py board_a board_b     # multiple boards in one release
python3 app/tools/bundle_firmware.py --add board_c       # merge into the current release instead of replacing it
python3 app/tools/bundle_firmware.py --dry-run           # report what would happen, write nothing
```

Or **Terminal → Run Task → Build release firmware** in the VS Code workspace.

Every manifest field (`app/firmware/manifest.json`) is derived from the sources and the resulting
binary — nothing is typed in by hand. Every board is built and validated before anything is
written, so a board that fails to compile leaves the previous release untouched rather than
half-replaced.

For the full mechanics (how the manifest is derived, why `config_hash.py` matters here too, DFU
vs. `esptool` dispatch), see the `bundle-firmware` skill and
"Firmware bundling and in-app updates" in `dev-docs/architecture.md`.
