#!/usr/bin/env bash
# Start the configurator backend (FastAPI + static web UI), replacing any
# instance of *this* project's server that is already running.
#
#   ./run-server.sh                 # http://127.0.0.1:8080
#   PORT=9000 ./run-server.sh       # different port
#   ./run-server.sh --reload        # extra args go straight to uvicorn
#
# Runs in the foreground: logs land in this terminal, Ctrl+C stops it.
set -euo pipefail

HOST="${HOST:-127.0.0.1}"
PORT="${PORT:-8080}"

REPO_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
APP_DIR="$REPO_DIR/app"
UVICORN="$APP_DIR/.venv/bin/uvicorn"

if [[ ! -x "$UVICORN" ]]; then
    echo "run-server: no venv at $APP_DIR/.venv" >&2
    echo "  create it with: python3 -m venv $APP_DIR/.venv &&" \
         "$APP_DIR/.venv/bin/pip install -r $APP_DIR/requirements.txt" >&2
    exit 1
fi

# Never signal our own process tree: a launching shell (VS Code task, wrapper
# script) can carry the search pattern in its command line, and killing it
# would take down the terminal along with the server.
declare -A ANCESTORS=()
_pid=$$
while [[ -n "$_pid" && "$_pid" != 0 ]]; do
    ANCESTORS[$_pid]=1
    _pid="$(ps -o ppid= -p "$_pid" 2>/dev/null | tr -d ' ')"
done

# A pid is "ours" if it is running this repo's uvicorn, or is a child of it
# (--reload forks a worker that inherits the cwd but not the cmdline).
is_ours() {
    local pid=$1 cwd cmd
    cwd="$(readlink -f "/proc/$pid/cwd" 2>/dev/null)" || return 1
    cmd="$(tr '\0' ' ' <"/proc/$pid/cmdline" 2>/dev/null)" || return 1
    [[ "$cmd" == *"$UVICORN"* ]] && return 0
    [[ "$cwd" == "$APP_DIR" && "$cmd" == *backend.main:app* ]]
}

mapfile -t port_pids < <(lsof -ti "tcp:$PORT" -sTCP:LISTEN 2>/dev/null || true)
mapfile -t named_pids < <(pgrep -f 'backend\.main:app' 2>/dev/null || true)

stale=()
for pid in "${port_pids[@]}" "${named_pids[@]}"; do
    [[ -n "$pid" && -z "${ANCESTORS[$pid]:-}" ]] || continue
    if is_ours "$pid"; then
        stale+=("$pid")
    elif [[ " ${port_pids[*]} " == *" $pid "* ]]; then
        # Something else owns the port. Killing it is not our call.
        echo "run-server: port $PORT is held by pid $pid, which is not this project:" >&2
        ps -o pid=,args= -p "$pid" >&2
        echo "  stop it yourself, or run with a different PORT=" >&2
        exit 1
    fi
done

if ((${#stale[@]})); then
    # Deduplicate: a pid can come from both lookups.
    mapfile -t stale < <(printf '%s\n' "${stale[@]}" | sort -un)
    echo "run-server: stopping existing server (pid ${stale[*]})"
    kill -TERM "${stale[@]}" 2>/dev/null || true
    for _ in {1..50}; do
        alive=()
        for pid in "${stale[@]}"; do
            kill -0 "$pid" 2>/dev/null && alive+=("$pid")
        done
        ((${#alive[@]})) || break
        sleep 0.1
    done
    if ((${#alive[@]})); then
        echo "run-server: pid ${alive[*]} ignored SIGTERM, sending SIGKILL" >&2
        kill -KILL "${alive[@]}" 2>/dev/null || true
        sleep 0.3
    fi
    # The old process can hold the listening socket briefly after exit.
    for _ in {1..50}; do
        lsof -ti "tcp:$PORT" -sTCP:LISTEN >/dev/null 2>&1 || break
        sleep 0.1
    done
fi

echo "run-server: http://$HOST:$PORT"
cd "$APP_DIR"
exec "$UVICORN" backend.main:app --host "$HOST" --port "$PORT" "$@"
