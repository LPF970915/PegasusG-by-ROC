#!/bin/sh
set -eu

ACTION="${1:-}"
STATE_DIR="${PEGASUSG_STATE_DIR:-/mnt/data/pegasusg-by-roc}"
RA_VOLUME_CFG="${PEGASUSG_RA_VOLUME_CFG:-/.config/retroarch/retroarch_volume.cfg}"
GAME_VOLUME_DB="$STATE_DIR/game-volume.db"
SCHEMA_FILE="$STATE_DIR/game-volume.schema"
LEGACY_MARKER="$STATE_DIR/game-volume-independent"
SCHEMA_VERSION=2

frontend_attenuation() {
  case "$1" in
    0) printf '%s\n' '-80.0' ;;
    1) printf '%s\n' '-29.8' ;;
    2) printf '%s\n' '-15.8' ;;
    3) printf '%s\n' '-10.7' ;;
    4) printf '%s\n' '-8.2' ;;
    5) printf '%s\n' '-5.7' ;;
    6) printf '%s\n' '-3.8' ;;
    7) printf '%s\n' '-2.2' ;;
    8) printf '%s\n' '-1.2' ;;
    *) printf '%s\n' '0.0' ;;
  esac
}

valid_db() {
  value="$1"
  case "$value" in ''|*[!0-9.-]*) return 1 ;; esac
  awk -v value="$value" 'BEGIN { exit !(value >= -80 && value <= 0) }'
}

read_ra_db() {
  value="$(sed -n 's/^audio_volume = "\([-0-9.]*\)".*/\1/p' "$RA_VOLUME_CFG" 2>/dev/null | head -n 1)"
  valid_db "$value" || value=0.0
  printf '%s\n' "$value"
}

write_atomic() {
  target="$1"
  value="$2"
  mkdir -p "$(dirname -- "$target")"
  temporary="$target.pegasusg.tmp.$$"
  printf '%s\n' "$value" >"$temporary"
  mv -f "$temporary" "$target"
}

write_ra_db() {
  value="$1"
  mkdir -p "$(dirname -- "$RA_VOLUME_CFG")"
  temporary="$RA_VOLUME_CFG.pegasusg.tmp.$$"
  printf 'audio_volume = "%s"\n' "$value" >"$temporary"
  mv -f "$temporary" "$RA_VOLUME_CFG"
}

initialize_game_db() {
  level="$(cat "$STATE_DIR/volume.level" 2>/dev/null || printf '6')"
  case "$level" in 0|1|2|3|4|5|6|7|8|9) ;; *) level=6 ;; esac
  current="$(read_ra_db)"
  attenuation="$(frontend_attenuation "$level")"
  if [ -f "$LEGACY_MARKER" ] && awk -v value="$current" 'BEGIN { exit !(value < -0.1) }'; then
    migrated="$current"
  else
    migrated="$(awk -v current="$current" -v attenuation="$attenuation" '
      BEGIN {
        value = current + attenuation
        if (value < -80) value = -80
        if (value > 0) value = 0
        printf "%.1f", value
      }')"
  fi
  write_atomic "$GAME_VOLUME_DB" "$migrated"
  write_atomic "$SCHEMA_FILE" "$SCHEMA_VERSION"
  : >"$LEGACY_MARKER"
}

prepare_volume() {
  case "${2:--1}" in
    0)
      level="$(cat "$STATE_DIR/volume.level" 2>/dev/null || printf '6')"
      case "$level" in 0|1|2|3|4|5|6|7|8|9) ;; *) level=6 ;; esac
      ;;
    1|2|3|4|5|6|7|8|9) level="$2" ;;
    *) level="" ;;
  esac
  if [ -n "$level" ]; then
    write_ra_db "$(frontend_attenuation "$level")"
    return
  fi
  schema="$(cat "$SCHEMA_FILE" 2>/dev/null || true)"
  value="$(cat "$GAME_VOLUME_DB" 2>/dev/null || true)"
  if [ "$schema" != "$SCHEMA_VERSION" ] || ! valid_db "$value"; then
    initialize_game_db
    value="$(cat "$GAME_VOLUME_DB")"
  fi
  write_ra_db "$value"
}

capture_volume() {
  value="$(read_ra_db)"
  valid_db "$value" || return 1
  write_atomic "$GAME_VOLUME_DB" "$value"
  write_atomic "$SCHEMA_FILE" "$SCHEMA_VERSION"
}

case "$ACTION" in
  prepare) prepare_volume "$@" ;;
  capture) capture_volume ;;
  *) echo "usage: $0 prepare [0=sync|1-9]|capture" >&2; exit 2 ;;
esac
