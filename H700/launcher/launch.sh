#!/bin/sh
set -u

APP_DIR="$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)"
BIN="$APP_DIR/pegasusg_by_roc"
STATE_DIR="${PEGASUSG_STATE_DIR:-/mnt/data/pegasusg-by-roc}"
REQUEST="$STATE_DIR/launch.request"
LOG_DIR="$STATE_DIR/logs"
LOG_FILE="$LOG_DIR/PegasusG-by-ROC.log"
LD_LIBRARY_PATH_BASE="${LD_LIBRARY_PATH:-}"
LD_PRELOAD_BASE="${LD_PRELOAD:-}"
SYSTEM_SDL=""
POWER_SCRIPT="${PEGASUSG_POWER_SCRIPT:-/mnt/vendor/ctrl/pwr_new.sh}"
FORCE_OS_SCRIPT="${PEGASUSG_FORCE_OS_SCRIPT:-/mnt/vendor/ctrl/forceOS.sh}"
CORE_ASSET_DIR="$APP_DIR/assets/cores"
FILTER_SCRIPT="$APP_DIR/tools/apply_filter.sh"
AUTO_CHEATS_SCRIPT="$APP_DIR/tools/apply_auto_cheats.sh"
GAME_OVERRIDES_SCRIPT="$APP_DIR/tools/apply_game_overrides.sh"
GAME_VOLUME_SCRIPT="$APP_DIR/tools/game_volume.sh"
CORE_DEST_DIR="/mnt/vendor/deep/retro/cores"
RA_LINK_DIR="${PEGASUSG_RA_LINK_DIR:-/mnt/data/pegasusg-ra/GBA}"
OS_SLEEP_NODE="/sys/class/power_supply/axp2202-battery/os_sleep"

mkdir -p "$STATE_DIR" "$LOG_DIR"

log_line() {
  printf '%s\n' "$*" >>"$LOG_FILE"
}

uptime_seconds() {
  cut -d ' ' -f 1 /proc/uptime 2>/dev/null || printf unknown
}

trim_log() {
  if [ -f "$LOG_FILE" ] && [ "$(wc -c <"$LOG_FILE" 2>/dev/null || echo 0)" -gt 262144 ]; then
    tail -c 131072 "$LOG_FILE" >"$LOG_FILE.tmp" 2>/dev/null && mv "$LOG_FILE.tmp" "$LOG_FILE"
  fi
}

detect_system_sdl() {
  if [ -f /usr/lib/aarch64-linux-gnu/libSDL2-2.0.so.0.2800.5 ]; then
    SYSTEM_SDL=/usr/lib/aarch64-linux-gnu/libSDL2-2.0.so.0.2800.5
  elif [ -f /roms/lib64/libSDL2-2.0.so.0.2800.6 ]; then
    SYSTEM_SDL=/roms/lib64/libSDL2-2.0.so.0.2800.6
  elif [ -f /usr/lib/aarch64-linux-gnu/libSDL2-2.0.so.0 ]; then
    SYSTEM_SDL=/usr/lib/aarch64-linux-gnu/libSDL2-2.0.so.0
  else
    return 1
  fi
}

set_frontend_runtime() {
  export LD_LIBRARY_PATH="/lib:/lib/aarch64-linux-gnu:/usr/lib/aarch64-linux-gnu:/usr/lib:/mnt/vendor/lib:/usr/lib32:${LD_LIBRARY_PATH_BASE}"
  export LD_PRELOAD="$SYSTEM_SDL${LD_PRELOAD_BASE:+:$LD_PRELOAD_BASE}"
}

restore_runtime() {
  export LD_LIBRARY_PATH="$LD_LIBRARY_PATH_BASE"
  if [ -n "$LD_PRELOAD_BASE" ]; then export LD_PRELOAD="$LD_PRELOAD_BASE"; else unset LD_PRELOAD; fi
}

run_frontend() {
  set_frontend_runtime
  log_line "[launcher] starting frontend uptime=$(uptime_seconds) driver=${SDL_VIDEODRIVER:-mali}"
  SDL_VIDEODRIVER="${SDL_VIDEODRIVER:-mali}" "$BIN" "$@" >>"$LOG_FILE" 2>&1
  rc=$?
  restore_runtime
  log_line "[launcher] frontend exited rc=$rc"
  return "$rc"
}

install_managed_core() {
  name="$1"
  source="$CORE_ASSET_DIR/$name"
  destination="$CORE_DEST_DIR/$name"
  if [ ! -f "$source" ]; then
    log_line "[launcher] managed core source missing: $source"
    return 1
  fi
  if [ -f "$destination" ] && cmp -s "$source" "$destination"; then
    return 0
  fi
  backup_dir="$STATE_DIR/core-backups"
  backup="$backup_dir/$name.original"
  if [ -f "$destination" ] && [ ! -f "$backup" ]; then
    mkdir -p "$backup_dir"
    cp -p "$destination" "$backup" 2>>"$LOG_FILE" ||
      log_line "[launcher] failed to preserve original core: $name"
  fi
  temporary="$destination.tmp.$$"
  if ! cp "$source" "$temporary" 2>>"$LOG_FILE"; then
    rm -f "$temporary"
    log_line "[launcher] failed to stage managed core: $name"
    return 1
  fi
  chmod 755 "$temporary"
  if ! mv -f "$temporary" "$destination" 2>>"$LOG_FILE"; then
    rm -f "$temporary"
    log_line "[launcher] failed to install managed core: $name"
    return 1
  fi
  sync
  log_line "[launcher] installed managed core: $name"
  return 0
}

install_managed_cores() {
  for name in mgba_libretro.so gpsp_libretro.so vbam_libretro.so vba_next_libretro.so \
      gpsp_rumble_libretro.so; do
    install_managed_core "$name" || true
  done
}

compatible_ra_rom_path() {
  source="$1"
  core_choice="${2:-}"
  case "$core_choice:$source" in
    gpsp:*.zip|gpsp_rumble:*.zip)
      mkdir -p "$RA_LINK_DIR" || return 1
      filename="${source##*/}"
      extracted="$RA_LINK_DIR/${filename%.*}.gba"
      rm -f "$extracted"
      if ! unzip -p "$source" >"$extracted" || [ ! -s "$extracted" ]; then
        rm -f "$extracted"
        log_line "[launcher] failed to extract gpSP ROM: $source"
        return 1
      fi
      printf '%s\n' "$extracted"
      return 0
      ;;
  esac
  case "$source" in
    */Roms/GBA\ hack/*|*/Roms/GBA\ vib/*)
      mkdir -p "$RA_LINK_DIR" || return 1
      link="$RA_LINK_DIR/${source##*/}"
      rm -f "$link"
      if ! ln -s "$source" "$link"; then
        log_line "[launcher] failed to create RA compatibility link for: $source"
        return 1
      fi
      printf '%s\n' "$link"
      ;;
    *) printf '%s\n' "$source" ;;
  esac
}

frontend_mixer_value() {
  case "$1" in
    0) printf '0\n' ;;
    1) printf '1\n' ;;
    2) printf '5\n' ;;
    3) printf '9\n' ;;
    4) printf '12\n' ;;
    5) printf '16\n' ;;
    6) printf '20\n' ;;
    7) printf '24\n' ;;
    8) printf '27\n' ;;
    *) printf '31\n' ;;
  esac
}

saved_frontend_volume() {
  level="$(cat "$STATE_DIR/volume.level" 2>/dev/null || printf '6')"
  case "$level" in 0|1|2|3|4|5|6|7|8|9) ;; *) level=6 ;; esac
  printf '%s\n' "$level"
}

set_game_hardware_volume() {
  (
    unset LD_PRELOAD
    amixer -q -c 0 set 'lineout volume' 31 &&
      amixer -q -c 0 set SPK on
  )
}

restore_frontend_hardware_volume() {
  level="$(saved_frontend_volume)"
  mixer="$(frontend_mixer_value "$level")"
  if [ "$level" -eq 0 ]; then
    (
      unset LD_PRELOAD
      amixer -q -c 0 set 'lineout volume' 0 &&
        amixer -q -c 0 set SPK off
    )
  else
    (
      unset LD_PRELOAD
      amixer -q -c 0 set 'lineout volume' "$mixer" &&
        amixer -q -c 0 set SPK on
    )
  fi
}

launch_requested_game() {
  [ -f "$REQUEST" ] || return 1
  IFS= read -r rom <"$REQUEST" || rom=""
  core_choice="$(sed -n '2p' "$REQUEST")"
  filter_mode="$(sed -n '3p' "$REQUEST")"
  system_volume="$(sed -n '4p' "$REQUEST")"
  case "$system_volume" in 0|1|2|3|4|5|6|7|8|9) ;; *) system_volume=-1 ;; esac
  rm -f "$REQUEST"
  case "$filter_mode" in calibrated|original|custom) ;; *) filter_mode=calibrated ;; esac
  case "$rom" in
    /mnt/mmc/*|/mnt/sdcard/*) ;;
    *) log_line "[launcher] rejected ROM path: $rom"; return 1 ;;
  esac
  [ -f "$rom" ] || { log_line "[launcher] ROM missing: $rom"; return 1; }
  launch_rom="$(compatible_ra_rom_path "$rom" "$core_choice")" || return 1
  case "$core_choice" in
    gpsp_rumble)
      if install_managed_core gpsp_rumble_libretro.so; then
        core=gpsp_rumble_libretro.so
        fallback=gpsp_libretro.so
      else
        core=gpsp_libretro.so
        fallback=mgba_libretro.so
      fi
      ;;
    gpsp) core=gpsp_libretro.so; fallback=mgba_libretro.so ;;
    vbam) core=vbam_libretro.so; fallback=mgba_libretro.so ;;
    vba_next) core=vba_next_libretro.so; fallback=mgba_libretro.so ;;
    *) core=mgba_libretro.so; fallback=gpsp_libretro.so ;;
  esac
  install_managed_core "$core" || log_line "[launcher] using existing core after install failure: $core"
  [ -f "/mnt/vendor/deep/retro/cores/$core" ] || core="$fallback"
  case "$core" in
    mgba_libretro.so) filter_core_choice=mgba ;;
    gpsp_libretro.so) filter_core_choice=gpsp ;;
    gpsp_rumble_libretro.so) filter_core_choice=gpsp_rumble ;;
    vbam_libretro.so) filter_core_choice=vbam ;;
    vba_next_libretro.so) filter_core_choice=vba_next ;;
    *) filter_core_choice=mgba ;;
  esac
  if [ -x "$FILTER_SCRIPT" ]; then
    "$FILTER_SCRIPT" "$filter_mode" "$filter_core_choice" ||
      log_line "[launcher] failed to apply filter mode=$filter_mode core=$filter_core_choice"
  else
    log_line "[launcher] filter script missing: $FILTER_SCRIPT"
  fi
  log_line "[timing] launch_request uptime=$(uptime_seconds)"
  log_line "[launcher] launch core=$core rom=$rom launch_rom=$launch_rom"
  if [ -x "$GAME_OVERRIDES_SCRIPT" ]; then
    "$GAME_OVERRIDES_SCRIPT" prepare-saves "$rom" ||
      log_line "[launcher] failed to redirect saves for ROM: $rom"
  else
    log_line "[launcher] game override script missing: $GAME_OVERRIDES_SCRIPT"
  fi
  game_volume_ready=0
  if [ -x "$GAME_VOLUME_SCRIPT" ] && "$GAME_VOLUME_SCRIPT" prepare "$system_volume"; then
    game_volume_ready=1
    set_game_hardware_volume || {
      game_volume_ready=0
      log_line "[launcher] failed to release game hardware volume"
    }
  else
    log_line "[launcher] game volume preparation failed; retaining frontend hardware volume"
  fi
  # The vendor RetroArch and cores are ARM32 even though this frontend is
  # AArch64. Match dmenu_ln's ABI-specific runtime before crossing processes.
  export LD_LIBRARY_PATH=/usr/lib32:/usr/lib:/mnt/vendor/lib
  unset LD_PRELOAD
  /mnt/mod/ctrl/RA_launch.sh "$core" "$launch_rom" auto >>"$LOG_FILE" 2>&1
  rc=$?
  if [ "$game_volume_ready" -eq 1 ] && [ "$system_volume" -eq -1 ]; then
    "$GAME_VOLUME_SCRIPT" capture || log_line "[launcher] failed to preserve game volume"
  fi
  if [ -x "$GAME_OVERRIDES_SCRIPT" ]; then
    "$GAME_OVERRIDES_SCRIPT" restore-saves ||
      log_line "[launcher] failed to restore RetroArch save directories"
  fi
  if [ "$launch_rom" != "$rom" ]; then rm -f "$launch_rom"; fi
  restore_runtime
  restore_frontend_hardware_volume || log_line "[launcher] failed to restore frontend volume"
  log_line "[launcher] RetroArch exited rc=$rc"
  return "$rc"
}

suspend_system() {
  automatic="$1"
  if [ ! -x "$POWER_SCRIPT" ]; then
    log_line "[launcher] power script missing: $POWER_SCRIPT"
    return 1
  fi
  # Super Standby (nonzero) disables hall wake. Lid sleep needs normal standby.
  sleep_mode=16
  if [ "$automatic" -eq 1 ]; then sleep_mode="${2:-0}"; fi
  if [ -e "$OS_SLEEP_NODE" ]; then
    if ! printf '%s' "$sleep_mode" >"$OS_SLEEP_NODE"; then
      log_line "[launcher] failed to set sleep mode=$sleep_mode"
      return 1
    fi
    log_line "[launcher] sleep mode=$sleep_mode"
  fi
  if [ "$automatic" -eq 1 ]; then
    log_line "[launcher] suspending reason=hall"
    "$POWER_SCRIPT" auto >>"$LOG_FILE" 2>&1
  else
    log_line "[launcher] suspending reason=power"
    "$POWER_SCRIPT" >>"$LOG_FILE" 2>&1
  fi
  rc=$?
  log_line "[launcher] resumed rc=$rc; restarting frontend with fresh SDL state"
  return "$rc"
}

change_power_state() {
  action="$1"
  if [ ! -x "$FORCE_OS_SCRIPT" ]; then
    log_line "[launcher] force OS script missing: $FORCE_OS_SCRIPT"
    return 1
  fi
  log_line "[launcher] power action=$action"
  "$FORCE_OS_SCRIPT" "$action" >>"$LOG_FILE" 2>&1
  rc=$?
  log_line "[launcher] power action returned rc=$rc"
  return "$rc"
}

if [ ! -x "$BIN" ]; then
  echo "PegasusG by ROC binary missing: $BIN" >&2
  exit 4
fi
detect_system_sdl || { echo "Vendor SDL2 runtime not found" >&2; exit 5; }
trim_log
printf '\n===== %s =====\n' "$(date '+%F %T %Z' 2>/dev/null || date)" >>"$LOG_FILE"
log_line "[timing] launcher_enter uptime=$(uptime_seconds)"
install_managed_cores
if [ -x "$GAME_OVERRIDES_SCRIPT" ]; then
  "$GAME_OVERRIDES_SCRIPT" restore-saves ||
    log_line "[launcher] failed to restore stale RetroArch save redirect"
  "$GAME_OVERRIDES_SCRIPT" install-core-options ||
    log_line "[launcher] failed to install mGBA game options"
else
  log_line "[launcher] game override script missing: $GAME_OVERRIDES_SCRIPT"
fi
if [ -x "$AUTO_CHEATS_SCRIPT" ]; then
  "$AUTO_CHEATS_SCRIPT" || log_line "[launcher] failed to install automatic GBA cheats"
else
  log_line "[launcher] automatic GBA cheat installer missing: $AUTO_CHEATS_SCRIPT"
fi

first_frontend=1
reuse_bgm=0

export SDL_AUDIODRIVER=alsa
export SDL_NOMOUSE=1
export HOME="$STATE_DIR/home"
export XDG_DATA_HOME="$STATE_DIR"
export XDG_CONFIG_HOME="$STATE_DIR/config"
export PEGASUSG_APP_DIR="$APP_DIR"
export PEGASUSG_DEVICE="${PEGASUSG_DEVICE:-h700}"
export PEGASUSG_STATE_DIR="$STATE_DIR"
export PEGASUSG_LAUNCH_REQUEST="$REQUEST"
export PEGASUSG_CONTENT_ROOTS="${PEGASUSG_CONTENT_ROOTS:-/mnt/mmc/Roms/GBA:/mnt/mmc/Roms/GBA hack:/mnt/mmc/Roms/GBA vib:/mnt/sdcard/Roms/GBA:/mnt/sdcard/Roms/GBA hack:/mnt/sdcard/Roms/GBA vib}"
export PEGASUSG_DIAGNOSTICS="${PEGASUSG_DIAGNOSTICS:-1}"
mkdir -p "$HOME" "$XDG_CONFIG_HOME"

while :; do
  if [ "$first_frontend" -eq 1 ]; then
    run_frontend "$@"
    rc=$?
    first_frontend=0
  elif [ "$reuse_bgm" -eq 1 ]; then
    run_frontend --restore-ui --reuse-bgm
    rc=$?
  else
    run_frontend --restore-ui
    rc=$?
  fi
  reuse_bgm=0
  if [ "$rc" -eq 20 ]; then
    launch_requested_game || true
    if [ "${PEGASUSG_EXIT_AFTER_GAME:-0}" = "1" ]; then
      exit 0
    fi
    continue
  fi
  if [ "$rc" -eq 21 ]; then
    suspend_system 0 || true
    reuse_bgm=1
    continue
  fi
  if [ "$rc" -eq 22 ] || [ "$rc" -eq 25 ]; then
    if [ "$rc" -eq 25 ]; then suspend_system 1 16 || true
    else suspend_system 1 || true
    fi
    reuse_bgm=1
    continue
  fi
  if [ "$rc" -eq 23 ]; then
    change_power_state rest
    exit $?
  fi
  if [ "$rc" -eq 24 ]; then
    change_power_state shutdwn
    exit $?
  fi
  exit "$rc"
done
