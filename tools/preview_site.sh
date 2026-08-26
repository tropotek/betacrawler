#!/usr/bin/env bash
# Build and serve the Pages site locally, the way .github/workflows/docs.yml does.
#
#   tools/preview_site.sh [REF] [PORT]
#
# REF defaults to main. Pass "release" for the newest version tag -- what the live
# site actually serves -- or any branch, tag or commit. Exports the ref with
# git archive, so the working tree and the current branch are untouched.
set -euo pipefail

REF="${1:-main}"
PORT="${2:-8777}"

REPO="$(git -C "$(dirname "${BASH_SOURCE[0]}")" rev-parse --show-toplevel)"
cd "$REPO"

if [ "$REF" = "release" ]; then
  REF="$(git tag --list '[0-9]*.[0-9]*.[0-9]*' --sort=-v:refname | head -n1)"
  [ -n "$REF" ] || { echo "no version tag exists yet; try 'main'" >&2; exit 1; }
fi
git rev-parse --verify --quiet "$REF^{commit}" >/dev/null \
  || { echo "no such ref: $REF" >&2; exit 1; }

MKDOCS="$REPO/docs/.venv/bin/mkdocs"
[ -x "$MKDOCS" ] || MKDOCS="$(command -v mkdocs || true)"
[ -n "$MKDOCS" ] || { echo "mkdocs not found; pip install -r tools/requirements.txt" >&2; exit 1; }

OUT="$(mktemp -d -t betacrawler-preview-XXXXXX)"
SERVER=""
# Kill the server explicitly: bash blocks in wait until signalled, and a signal
# reaching only this shell would otherwise orphan it and leave the build behind.
cleanup() {
  [ -n "$SERVER" ] && kill "$SERVER" 2>/dev/null || true
  rm -rf "$OUT"
}
trap cleanup EXIT INT TERM

git archive "$REF" | tar -x -C "$OUT"
"$MKDOCS" build --strict -f "$OUT/mkdocs.yml" -d "$OUT/site"

# The configurator is a static site with no build step, so it ships as-is.
cp -r "$OUT/web-app" "$OUT/site/app"
# /app-dev/ always tracks main, whatever ref the docs came from.
git archive main web-app | tar -x -C "$OUT" --transform 's|^web-app|app-dev|'
mv "$OUT/app-dev" "$OUT/site/app-dev"

echo
echo "  $(git log --format='%h %s' -1 "$REF")"
echo "  docs + /app/ from $REF, /app-dev/ from main"
echo "  http://127.0.0.1:$PORT/"
echo "  Ctrl-C to stop; the build is deleted on exit."
echo
cd "$OUT/site"
python3 -m http.server "$PORT" --bind 127.0.0.1 &
SERVER=$!
wait "$SERVER"
