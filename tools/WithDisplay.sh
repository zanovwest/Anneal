#!/usr/bin/env bash
# Run native GUI checks on the user's desktop or a complete disposable X11 desktop.
set -euo pipefail

if [[ "${1:-}" == --xvfb-session ]]; then
    shift
    openbox --sm-disable &
    wm_pid=$!
    cleanup() { kill "$wm_pid" 2>/dev/null || true; wait "$wm_pid" 2>/dev/null || true; }
    trap cleanup EXIT
    ready=0
    for ((attempt=0; attempt<50; ++attempt)); do
        if xprop -root _NET_SUPPORTING_WM_CHECK 2>/dev/null | grep -q 'window id # 0x'; then
            ready=1; break
        fi
        kill -0 "$wm_pid" 2>/dev/null || break
        sleep 0.1
    done
    ((ready)) || { echo "The test window manager did not become ready." >&2; exit 2; }
    "$@"
elif [[ -n "${DISPLAY:-}" ]]; then
    exec "$@"
else
    for program in xvfb-run openbox xprop; do
        command -v "$program" >/dev/null || {
            echo "Headless GUI checks need xvfb, xauth, openbox and x11-utils (missing $program)." >&2
            exit 2
        }
    done
    # JUCE 8.0.4 expects WM_PROTOCOLS and other window-manager atoms to exist.
    # Bare Xvfb has no window manager and opening the VST3 editor raises BadAtom.
    exec xvfb-run -a -s '-screen 0 1920x1080x24' bash "$0" --xvfb-session "$@"
fi
