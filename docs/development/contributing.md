# Contributing

This page is a stub — it'll grow as the project's contribution process settles. For now, here's
the basics.

## Reporting an issue

Open an issue on [GitHub](https://github.com/tropotek/betacrawler/issues). Include what board
you're on, the firmware/app version shown in the footer, and steps to reproduce.

## Making a change

1. Fork the repo and create a branch off `main` for your change.
2. Follow [Setup](setup.md) to get firmware, the web app and the docs running locally.
3. Make your change. `firmware/` tests with `pio test -e native`, `web-app/` with `node --test`.
4. Commit with a `feat:`/`fix:`/`docs:`/`chore:` prefix describing the change.
5. Push your branch and open a pull request against `main` on GitHub.

Merging to `main` publishes nothing to users — the docs and the configurator at
[/app/](https://tropotek.github.io/betacrawler/app/) are built from the newest release tag.
`main` as it stands is served at
[/app-dev/](https://tropotek.github.io/betacrawler/app-dev/), unlisted and unsupported, for
testing a merge against real hardware. Cutting a tag is [Releasing](releasing.md).
