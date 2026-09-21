#!/usr/bin/env bash
# Throwaway: run the editor under a watchdog and capture what it is doing if
# it stops answering. Delete this file once the freeze is understood.
#
#   ./debug-freeze.sh              launch from here
#   ./debug-freeze.sh /some/dir    launch from there instead
#
# Use the editor normally. When it wedges, this writes freeze-report.txt and
# keeps watching. Ctrl-C to stop.

set -u

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BIN="$HERE/build/satl-source"
REPORT="$HERE/freeze-report.txt"
START_IN="${1:-$PWD}"

: > "$REPORT"
exec 3>&1
say() { echo "$@" | tee -a "$REPORT" >&3; }

say "=== satl-source freeze watch ==="
say "date:        $(date)"
say "binary:      $BIN ($(stat -c %y "$BIN" 2>/dev/null))"
say "launched in: $START_IN"
say "session:     ${XDG_SESSION_TYPE:-?} / ${XDG_CURRENT_DESKTOP:-?}"
say "environment: $(env | grep -E '^(GSK_|GDK_|GTK_|WAYLAND_DISPLAY|DISPLAY)=' | tr '\n' ' ')"
say "gnome-shell: $(gnome-shell --version 2>/dev/null)"
say ""

cd "$START_IN" || exit 1
"$BIN" > >(tee -a "$REPORT" >&3) 2>&1 &
APP=$!
say "started pid $APP -- use the window normally"
say ""

probe() {   # does the app still answer an outside request?
    timeout 6 python3 - "$1" <<'PY' >/dev/null 2>&1
import pyatspi, sys
pid = int(sys.argv[1])
for app in pyatspi.Registry.getDesktop(0):
    try:
        if app and app.name and 'satl' in app.name.lower():
            for i in range(app.childCount):
                app.getChildAtIndex(i).getRoleName()
            sys.exit(0)
    except Exception:
        pass
sys.exit(1)
PY
}

dump() {
    say ""
    say "############ NOT RESPONDING at $(date +%H:%M:%S) ############"
    say "--- per-thread state (wchan tells you what each thread is waiting on) ---"
    ps -L -o tid,stat,%cpu,wchan:28,comm -p "$APP" >> "$REPORT" 2>&1
    say "--- all thread backtraces ---"
    gdb -p "$APP" -batch -ex "set pagination off" -ex "thread apply all bt" >> "$REPORT" 2>&1
    say "############ end of capture ############"
    say ""
    say "Written to $REPORT -- tell Claude it is there."
}

misses=0
while kill -0 "$APP" 2>/dev/null; do
    sleep 3
    if probe "$APP"; then
        misses=0
    else
        misses=$((misses + 1))
        say "[$(date +%H:%M:%S)] no answer ($misses)"
        if [ "$misses" -ge 2 ]; then
            dump
            misses=0
        fi
    fi
done
say "app exited"
