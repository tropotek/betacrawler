# Releasing

`main` is a staging branch. Merging to it publishes nothing to users — the docs and the
configurator they flash from come from the newest release tag. A release is a tag.

## One version number

`FW_VERSION` (`firmware/include/config.h`), `APP_VERSION` (`web-app/js/app.js`) and the git tag
are the same `X.Y.Z` string, bumped in a single commit. Everything else derives from those two
constants: `web-app/firmware/manifest.json`, the board's `fw` string reported by `hello`, and the
version in the app footer. Nothing renders a shortened form, so the string a user reads in the app
and the string their board reports are directly comparable.

CHANGELOG headings use the same token: `## 4.2.0`.

## What each digit means

- **MAJOR** — the maintainer's decision. No code change forces it.
- **MINOR** and **PATCH** — moved as the work warrants: PATCH for fixes, docs and small
  corrections, MINOR when features land or enough has accumulated.

The number does not say whether a release wipes stored settings, so the CHANGELOG does. Any
release that changes the settings fingerprint opens its section with:

> **Saved settings reset to defaults on the first boot after flashing.**

## Cutting a release

1. Bump `FW_VERSION` and `APP_VERSION` together.
2. Write the CHANGELOG section under an `## X.Y.Z` heading, including the settings-reset line
   above if the fingerprint moved.
3. Run the firmware bundler and commit `web-app/firmware/` — the tagged site flashes those images,
   and `web-app/tests/firmware-bundle.test.js` fails if they no longer match the sources.
4. `pio test -e native` from `firmware/`, `node --test` from `web-app/`.
5. Tag `main` and push:

    ```
    git tag -a 4.2.0 -m "betacrawler 4.2.0"
    git push origin 4.2.0
    ```

Tags are bare `X.Y.Z` — no `v` prefix — and annotated. The push triggers two workflows: the site
rebuilds against the new tag, and a GitHub Release is created carrying both board images and that
version's CHANGELOG section, for anyone flashing over ST-Link rather than WebUSB.

## What gets published where

| URL | Built from |
|---|---|
| <https://tropotek.github.io/betacrawler/> | newest release tag |
| <https://tropotek.github.io/betacrawler/app/> | newest release tag |
| <https://tropotek.github.io/betacrawler/app-dev/> | `main` |

`/app-dev/` is the configurator as it stands on `main`, for testing a merge against real hardware
before tagging. It is unlisted and unsupported; the firmware images it offers are whatever was
last bundled on `main`.

`.github/workflows/docs.yml` builds all three from one job and uploads them as a single Pages
artifact — a deploy replaces the whole site, so the release site and the dev app are always built
together.
