# Contributing

Contributions are welcome — firmware, the web app, the docs, or a build log of a chassis you got
working. This page covers the process and the conventions a change is expected to follow.

## Reporting an issue

Open an issue on [GitHub](https://github.com/tropotek/betacrawler/issues). Include what board
you're on, the firmware/app version shown in the footer, and steps to reproduce. If it involves a
receiver, say which protocol and which ELRS version — only ELRS is verified against this firmware.

## Making a change

1. Fork the repo and create a branch off `main` for your change.
2. Follow [Setup](setup.md) to get firmware, the web app and the docs running locally.
3. Make your change, following the conventions below.
4. Run the tests. `firmware/` tests with `pio test -e native`, `web-app/` with `node --test`.
5. Commit with a `feat:`/`fix:`/`docs:`/`chore:` prefix describing the change.
6. Push your branch and open a pull request against `main` on GitHub.

Small, focused pull requests get reviewed faster than large ones. If a change touches an area with
a rule below, say in the PR description how you handled it.

## Conventions

Most of these are enforced by a test, so the fastest way to discover them is to run the suites
before pushing. The reasoning behind each lives in
[Architecture](architecture.md) — read the relevant section before changing that area.

**Firmware modules.** `firmware/src/core/` is pure C++ with zero Arduino includes, and never names
a feature. Each module is a `<name>_params.cpp` carrying its `ModuleDesc` — also Arduino-free, so
the native build compiles it — plus a `<name>_driver.cpp` that touches hardware. A `ParamDef`
never goes in a driver file. Modules receive module-local parameter indices, never global ones.
See [Modules](architecture.md#modules).

**Observers are const.** A module gets const access to another through `attach()`, and must not
reconfigure one behind the dispatcher's back. Resolve keys once in `attach()`, not per tick. See
[Observing modules](architecture.md#observing-modules).

**The `Api` seam.** In `web-app/`, nothing outside `js/api.js` makes a serial, WebUSB or network
call to the device, and no browser-only type crosses that boundary. See
[The `Api` seam](architecture.md#the-api-seam).

**Validation is schema-driven; page curation isn't.** The firmware validates every parameter
regardless of whether any page displays it, so Terminal `set` and INI restore keep working. The
config, controller and modes pages hand-pick the keys they show. Adding a firmware parameter
therefore needs a decision about which page it belongs on and a written label, or it appears
nowhere. See [Wire protocol and the schema-driven UI](architecture.md#wire-protocol-and-the-schema-driven-ui).

**Re-bundle firmware images.** `web-app/firmware/` is committed, because the site is static and has
no server to build an image on demand. After any change to firmware sources, run
`python3 tools/bundle_firmware.py --all` and commit the result in the same PR.
`web-app/tests/firmware-bundle.test.js` fails when the committed binaries have fallen behind the
sources. See [Firmware bundling and in-app updates](architecture.md#firmware-bundling-and-in-app-updates).

**Page fragments end with a spacer.** Every file in `web-app/pages/` ends with `<p>&nbsp;</p>` as
its last child, so content doesn't sit flush against the viewport bottom. Add one to a new page;
don't strip existing ones as empty markup.

**Don't bump the version.** `FW_VERSION` and `APP_VERSION` move together, in one commit, at
release time — and the maintainer decides which digit moves. Leave both alone in a PR; merged work
waits under `## Unreleased` in `CHANGELOG.md`. See [Releasing](releasing.md).

**Specs and plans aren't committed.** `_notes/` is gitignored and is the place for design notes,
plans and research. `docs/` is the published site — anything added there ships to users.

## Testing

Both suites run without any hardware attached:

```bash
cd firmware && ~/.platformio/penv/bin/pio test -e native   # native C++ unit tests
cd web-app  && node --test                                  # unit tests and browser suites
```

`node --test` also drives the rendered UI in a real headless Chromium. Those browser suites skip
visibly if the Playwright environment isn't installed — [Setup](setup.md) covers installing it.
Add a browser suite by dropping a new `tests/<name>.playwright.py` into `web-app/tests/`; it is
discovered, not registered in a list.

A change to firmware that can only be verified on hardware should say so in the PR, and say what
you tested it on — board, receiver, and motor type.

## Documentation

Comments and docs describe the project as it currently is, not as a narrative of what changed.
History belongs in `CHANGELOG.md` and commit messages. Comments stay minimal: what the code does,
and its config options if it has any.

Docs changes preview locally with `docs/.venv/bin/mkdocs serve`, and the whole published site —
docs plus both configurator builds — with `tools/preview_site.sh`.

## After a merge

Merging to `main` publishes nothing to users. The docs and the configurator at
[/app/](https://tropotek.github.io/betacrawler/app/) are built from the newest release tag. `main`
as it stands is served at [/app-dev/](https://tropotek.github.io/betacrawler/app-dev/), unlisted
and unsupported, for testing a merge against real hardware. Cutting a tag is
[Releasing](releasing.md).
