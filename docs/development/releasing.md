# Releasing

`main` is a staging branch. Merging to it publishes nothing to users — the docs and the
configurator they flash from come from the newest release tag. A release is a tag.

## One version number

`FW_VERSION` (`firmware/include/config.h`), `APP_VERSION` (`web-app/js/app.js`) and the git tag
are the same `X.Y.Z` string, bumped in a single commit. Everything else derives from those two
constants: `web-app/firmware/manifest.json`, the board's `fw` string reported by `hello`, and the
version in the app footer. Nothing renders a shortened form, so the string a user reads in the app
and the string their board reports are directly comparable.

CHANGELOG headings use the same token: `## 4.2.0`. Merged work accumulates under `## Unreleased`
until a release is cut, which renames that heading to the version being tagged.

## What each digit means

- **PATCH** — the default. Fixes, docs, features, refactors: unless there is a specific reason
  to move another digit, the patch number moves.
- **MINOR** — the maintainer's decision, and it needs a compelling reason. Features landing is
  not one. The clearest case is a firmware change a user cannot carry forward — settings or
  hardware that will not survive the update — where the version itself should warn them.
- **MAJOR** — the maintainer's decision. No code change forces it.

Keeping MINOR still is deliberate: ongoing work should not push it into triple digits. The cost
of a disruptive change is also low while the project has few users and few settings, so
reconfiguring after a flash is quick. That is a condition, not a law — as real users arrive and
the parameter set grows, the bar for calling something PATCH rises with it.

The number does not say whether a release wipes stored settings, so the CHANGELOG does. Any
release that changes the settings fingerprint opens its section with:

> **Saved settings reset to defaults on the first boot after flashing.**

## Cutting a release

The version number is the maintainer's decision. Several merges may wait under `## Unreleased`
before any of them is released.

1. Bump `FW_VERSION` and `APP_VERSION` together.
2. Rename `## Unreleased` to `## X.Y.Z`, and check it carries the settings-reset line above if the
   fingerprint moved.
3. Run the firmware bundler and commit `web-app/firmware/` — the tagged site flashes those images,
   and `web-app/tests/firmware-bundle.test.js` fails if they no longer match the sources.
4. `pio test -e native` from `firmware/`, `node --test` from `web-app/`.
5. `tools/preview_site.sh main` and read the site you are about to publish. Merging to `main`
   publishes nothing, so this is the last chance to see it before the tag makes it live.
6. Tag `main` and push:

    ```
    git tag -a 4.2.0 -m "BetaCrawler 4.2.0"
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
together. It runs on pushes to `main` and resolves the newest tag itself, so a push publishes the
released site rather than main's; `.github/workflows/release.yml` dispatches it after tagging.
Deploys run only from `main`: a Pages deployment created from a tag ref reports success but is
never promoted to the live site.
