---
name: bundle-firmware
description: Build and bundle firmware images into app/firmware/ for release — run after any firmware change worth shipping.
---

**Firmware bundle** (from the repo root), after any firmware change worth shipping. `app/firmware/`
is gitignored build output, so a fresh checkout has none until this is run:
```
python3 app/tools/bundle_firmware.py                    # builds, then updates app/firmware/
python3 app/tools/bundle_firmware.py board_a board_b    # the whole release set, in one go
python3 app/tools/bundle_firmware.py --add other_board  # merge, don't prune the rest
python3 app/tools/bundle_firmware.py --dry-run          # report only
```
Also the **Build release firmware** task in `silkscreen.code-workspace`.
