---
name: bundle-firmware
description: Build and bundle firmware images into app/firmware/ and web-app/firmware/ for release — run after any firmware change worth shipping.
---

**Firmware bundle** (from the repo root), after any firmware change worth shipping. One run writes
both destinations: `app/firmware/`, which is gitignored build output a fresh checkout has none of,
and `web-app/firmware/`, which is committed because the static site has no backend to build an
image on demand:
```
python3 app/tools/bundle_firmware.py                    # builds, then updates app/firmware/
python3 app/tools/bundle_firmware.py board_a board_b    # the whole release set, in one go
python3 app/tools/bundle_firmware.py --all              # every board target in one run
python3 app/tools/bundle_firmware.py --add other_board  # merge, don't prune the rest
python3 app/tools/bundle_firmware.py --dry-run          # report only
```
Also the **Build release firmware** task in `betacrawler.code-workspace`.
There is also a no-prompt **Build ALL release firmware** task for `--all`.

**Then commit `web-app/firmware/` and check the guard**, in the same commit as the firmware change
that prompted the rebuild:
```
cd web-app && node --test          # firmware-bundle.test.js must pass
git add web-app/firmware
```
That test recomputes the manifest's `fw_source_sha256` from `firmware/{include,src}` and
`platformio.ini`. It fails whenever the committed binaries have fallen behind the sources — which
a version number alone cannot catch, since not every source change bumps it.
