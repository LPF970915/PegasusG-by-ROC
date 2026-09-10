#!/bin/sh
set -eu

ROOT="$(mktemp -d)"
trap 'rm -rf "$ROOT"' EXIT INT TERM
export OS_SLEEP_NODE="$ROOT/os_sleep"
export POWER_CALLS="$ROOT/power.calls"
POWER_SCRIPT="$ROOT/power.sh"
LOG_FILE="$ROOT/launcher.log"
log_line() { printf '%s\n' "$*" >>"$LOG_FILE"; }

awk '/^suspend_system\(\)/ { copy=1 } copy { print } copy && /^}/ { exit }' \
  H700/launcher/launch.sh >"$ROOT/suspend.sh"
. "$ROOT/suspend.sh"
cat >"$POWER_SCRIPT" <<'EOF'
#!/bin/sh
printf '%s:%s\n' "${1:-power}" "$(cat "$OS_SLEEP_NODE" 2>/dev/null || printf missing)" >>"$POWER_CALLS"
exit "${POWER_RESULT:-0}"
EOF
chmod 755 "$POWER_SCRIPT"

printf 16 >"$OS_SLEEP_NODE"
suspend_system 1
test "$(cat "$POWER_CALLS")" = auto:0
suspend_system 0
test "$(tail -n 1 "$POWER_CALLS")" = power:16
suspend_system 1
test "$(tail -n 1 "$POWER_CALLS")" = auto:0

export POWER_RESULT=7
if suspend_system 1; then exit 1; else test "$?" -eq 7; fi
unset POWER_RESULT
rm "$OS_SLEEP_NODE"
suspend_system 0
test "$(tail -n 1 "$POWER_CALLS")" = power:missing

mkdir "$OS_SLEEP_NODE"
calls="$(wc -l <"$POWER_CALLS")"
if suspend_system 1 2>/dev/null; then exit 1; fi
test "$(wc -l <"$POWER_CALLS")" -eq "$calls"
rm -r "$OS_SLEEP_NODE"
rm "$POWER_SCRIPT"
if suspend_system 1; then exit 1; fi
test "$(wc -l <"$POWER_CALLS")" -eq "$calls"
