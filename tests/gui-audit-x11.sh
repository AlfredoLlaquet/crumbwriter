#!/bin/sh
# Run the appearance audit on an X display with a window manager handling focus and dialogs.
# The caller supplies an isolated X server and D-Bus session.
set -eu
manager=$1
audit=$2
output=$3
mkdir -p "$output"
export QT_QPA_PLATFORM=xcb
export KWIN_COMPOSE=N QT_QUICK_BACKEND=software
"$manager" >"$output/window-manager.log" 2>&1 &
manager_pid=$!
trap 'kill "$manager_pid" 2>/dev/null || :; wait "$manager_pid" 2>/dev/null || :' EXIT
attempt=0
until xprop -root _NET_SUPPORTING_WM_CHECK | grep -Eq 'window id # 0x[1-9a-fA-F]'; do
    if ! kill -0 "$manager_pid" 2>/dev/null || [ "$attempt" -ge 100 ]; then
        cat "$output/window-manager.log" >&2
        echo "$manager did not acquire the X display" >&2
        exit 1
    fi
    attempt=$((attempt + 1))
    sleep 0.1
done
"$audit" --output "$output" --platform-name xcb
