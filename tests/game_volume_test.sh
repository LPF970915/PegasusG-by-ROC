#!/bin/sh
set -eu

ROOT="${TMPDIR:-/tmp}/pegasusg-game-volume-test-$$"
STATE="$ROOT/state"
RA_VOLUME="$ROOT/retroarch_volume.cfg"
SCRIPT="H700/launcher/game_volume.sh"

cleanup() {
  rm -rf "$ROOT"
}
trap cleanup EXIT INT TERM
mkdir -p "$STATE"

run_volume() {
  PEGASUSG_STATE_DIR="$STATE" PEGASUSG_RA_VOLUME_CFG="$RA_VOLUME" sh "$SCRIPT" "$@"
}

printf '2\n' >"$STATE/volume.level"
printf 'audio_volume = "0.0"\n' >"$RA_VOLUME"
run_volume prepare
grep -q '^audio_volume = "-15.8"$' "$RA_VOLUME"
grep -q '^-15.8$' "$STATE/game-volume.db"

# A volume selected inside RetroArch persists independently from frontend volume.
printf 'audio_volume = "-3.0"\n' >"$RA_VOLUME"
run_volume capture
printf 'audio_volume = "0.0"\n' >"$RA_VOLUME"
run_volume prepare
grep -q '^audio_volume = "-3.0"$' "$RA_VOLUME"

# Upgrade an old migrated installation without applying attenuation twice.
rm -f "$STATE/game-volume.db" "$STATE/game-volume.schema"
: >"$STATE/game-volume-independent"
printf 'audio_volume = "-8.2"\n' >"$RA_VOLUME"
run_volume prepare
grep -q '^audio_volume = "-8.2"$' "$RA_VOLUME"

# An old 0 dB state is rebased once to the current low frontend volume.
rm -f "$STATE/game-volume.db" "$STATE/game-volume.schema"
printf 'audio_volume = "0.0"\n' >"$RA_VOLUME"
run_volume prepare
grep -q '^audio_volume = "-15.8"$' "$RA_VOLUME"

# Sync and fixed levels leave legacy independent volume intact.
printf '0.0\n' >"$STATE/game-volume.db"
printf '2\n' >"$STATE/game-volume.schema"
run_volume prepare 0
grep -q '^audio_volume = "-15.8"$' "$RA_VOLUME"
grep -q '^0.0$' "$STATE/game-volume.db"
for entry in '9 0.0' '1 -29.8' '0 -80.0' '6 -3.8'; do
  set -- $entry
  printf '%s\n' "$1" >"$STATE/volume.level"
  run_volume prepare 0
  grep -q "^audio_volume = \"$2\"$" "$RA_VOLUME"
done
run_volume prepare -1
grep -q '^audio_volume = "0.0"$' "$RA_VOLUME"

# A fixed system level is unaffected by the interface volume.
printf '0\n' >"$STATE/volume.level"
run_volume prepare 9
grep -q '^audio_volume = "0.0"$' "$RA_VOLUME"
printf '9\n' >"$STATE/volume.level"
run_volume prepare 2
grep -q '^audio_volume = "-15.8"$' "$RA_VOLUME"

# Empty saved volume must use the frontend default, not a legacy loud setting.
: >"$STATE/volume.level"
run_volume prepare 0
grep -q '^audio_volume = "-3.8"$' "$RA_VOLUME"
