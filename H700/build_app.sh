#!/bin/bash
set -euo pipefail

SELF_DIR="$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)"
REPO_ROOT="$(CDPATH= cd -- "$SELF_DIR/.." && pwd)"

APP_ID="${PEGASUSG_APP_ID:-PegasusG by ROC}"
APP_ENTRY_NAME="${PEGASUSG_APP_ENTRY_NAME:-PegasusG by ROC}"
BINARY_NAME="${PEGASUSG_BINARY_NAME:-pegasusg_by_roc}"
VERSION="${PEGASUSG_VERSION:-1.08}"
OUTPUT="${PEGASUSG_OUTPUT:-Stage}"
BUILDER="${PEGASUSG_BUILDER:-Sysroot}"
SKIP_BUILD="${PEGASUSG_SKIP_BUILD:-0}"
SYSROOT="${PEGASUSG_SYSROOT:-$REPO_ROOT/H700/sysroot}"
MUSIC_SOURCE="${PEGASUSG_MUSIC_SOURCE:-$REPO_ROOT/assets/music/builtin}"
FONT_SOURCE="${PEGASUSG_FONT_SOURCE:-}"

if [ -n "${CROSS_CXX:-}" ]; then
  CXX_CMD="$CROSS_CXX"
elif command -v aarch64-linux-gnu-g++-11 >/dev/null 2>&1; then
  CXX_CMD="aarch64-linux-gnu-g++-11"
else
  CXX_CMD="aarch64-linux-gnu-g++"
fi
PKG_CMD="${CROSS_PKG_CONFIG:-pkg-config}"
READ_ELF="${CROSS_READELF:-aarch64-linux-gnu-readelf}"

BUILD_DIR="$REPO_ROOT/build/h700"
DIST_ROOT="$SELF_DIR/dist_app"
STAGE_ROOT="$DIST_ROOT/release_stage"
RUNTIME="$STAGE_ROOT/Roms/APPS/$APP_ID"
ARCHIVE="$SELF_DIR/Downloads/PegasusG by ROC ver${VERSION} for H700 - Full Package.zip"

case "$OUTPUT" in Stage|Zip) ;; *) echo "[h700] PEGASUSG_OUTPUT must be Stage or Zip" >&2; exit 2 ;; esac

case "$BUILDER" in Sysroot|Docker) ;; *) echo "[h700] PEGASUSG_BUILDER must be Sysroot or Docker" >&2; exit 2 ;; esac

if [ "$SKIP_BUILD" != 1 ]; then
  mkdir -p "$BUILD_DIR"
  if [ "$BUILDER" = Docker ]; then
    image="${PEGASUSG_BUILDER_IMAGE:-ghcr.io/monkeyx-net/portmaster-build-templates/portmaster-builder:aarch64-latest}"
    docker image inspect "$image" >/dev/null 2>&1 || docker pull --platform=linux/arm64 "$image"
    compiler=(docker run --rm --pull=never --network=none --platform=linux/arm64
      -v "$REPO_ROOT:$REPO_ROOT:ro" -v "$BUILD_DIR:$BUILD_DIR"
      -e "PEGASUSG_BINARY=$BUILD_DIR/$BINARY_NAME" "$image" bash -c '
        set -e
        g++ "$@" $(pkg-config --cflags --libs sdl2 SDL2_image SDL2_ttf alsa)
        strip "$PEGASUSG_BINARY"
        "$PEGASUSG_BINARY" --help
      ' --)
    compiler_flags=()
    pkg_cflags=""
    pkg_libs=""
  else
    if [ ! -d "$SYSROOT/usr/include" ] || [ ! -d "$SYSROOT/usr/lib" ]; then
      echo "[h700] invalid sysroot: $SYSROOT" >&2
      exit 1
    fi
    command -v "$CXX_CMD" >/dev/null 2>&1 || { echo "[h700] missing compiler: $CXX_CMD" >&2; exit 1; }

    find_pkg_dirs() {
      for d in \
        "$SYSROOT/usr/lib/aarch64-linux-gnu/pkgconfig" \
        "$SYSROOT/lib/aarch64-linux-gnu/pkgconfig" \
        "$SYSROOT/usr/lib/pkgconfig" \
        "$SYSROOT/lib/pkgconfig" \
        "$SYSROOT/usr/share/pkgconfig"; do
        [ -d "$d" ] && printf "%s:" "$d"
      done
    }

    PKG_LIBDIR="$(find_pkg_dirs || true)"
    PKG_LIBDIR="${PKG_LIBDIR%:}"
    export PKG_CONFIG_SYSROOT_DIR="$SYSROOT"
    export PKG_CONFIG_LIBDIR="$PKG_LIBDIR"
    export PKG_CONFIG_PATH=""

    pkg_cflags="$("$PKG_CMD" --cflags sdl2 SDL2_image SDL2_ttf 2>/dev/null || true)"
    pkg_libs="$("$PKG_CMD" --libs sdl2 SDL2_image SDL2_ttf alsa 2>/dev/null || true)"
    if [ -z "$pkg_cflags" ]; then
      pkg_cflags="-I$SYSROOT/usr/include/SDL2 -I$SYSROOT/usr/include -D_REENTRANT"
    fi
    if [ -z "$pkg_libs" ]; then
      pkg_libs="-L$SYSROOT/usr/lib -L$SYSROOT/usr/lib/aarch64-linux-gnu -L$SYSROOT/lib/aarch64-linux-gnu -lSDL2_image -lSDL2_ttf -lSDL2 -lasound"
    fi
    compiler=("$CXX_CMD")
    compiler_flags=(--sysroot="$SYSROOT"
      -Wl,-rpath-link,"$SYSROOT/usr/lib/aarch64-linux-gnu"
      -Wl,-rpath-link,"$SYSROOT/lib/aarch64-linux-gnu"
      -Wl,-rpath-link,"$SYSROOT/usr/lib"
      -Wl,-rpath-link,"$SYSROOT/lib"
      -Wl,--allow-shlib-undefined)
  fi
  "${compiler[@]}" "${compiler_flags[@]}" \
    -std=c++17 -O2 -DNDEBUG -Wall -Wextra -Wpedantic -pthread \
    -I"$REPO_ROOT/src" $pkg_cflags \
    "$REPO_ROOT/src/main.cpp" \
    "$REPO_ROOT/src/gba_frontend.cpp" \
    "$REPO_ROOT/src/gba_preferences.cpp" \
    "$REPO_ROOT/src/gba_state.cpp" \
    "$REPO_ROOT/src/gba_ui_state.cpp" \
    "$REPO_ROOT/src/h700_services.cpp" \
    "$REPO_ROOT/src/library_scanner.cpp" \
    "$REPO_ROOT/src/optimized_image_path.cpp" \
    "$REPO_ROOT/src/pegasus_metadata.cpp" \
    "$REPO_ROOT/src/video_preview.cpp" \
    -o "$BUILD_DIR/$BINARY_NAME" \
    $pkg_libs \
    -pthread
fi

rm -rf "$STAGE_ROOT"
mkdir -p "$RUNTIME/assets/cheats" "$RUNTIME/assets/cores" "$RUNTIME/assets/core_options/mGBA" "$RUNTIME/assets/filters/shaders" "$RUNTIME/assets/recommended_controls/core" "$RUNTIME/assets/recommended_controls/remaps" "$RUNTIME/assets/splash" "$RUNTIME/assets/fonts" "$RUNTIME/assets/music" "$RUNTIME/assets/ui" "$RUNTIME/config" \
  "$RUNTIME/tools" "$RUNTIME/third_party/licenses" \
  "$RUNTIME/data/cache" "$RUNTIME/data/conf" "$RUNTIME/data/home" \
  "$STAGE_ROOT/Roms/APPS/Imgs" "$SELF_DIR/Downloads"
cp "$BUILD_DIR/$BINARY_NAME" "$RUNTIME/$BINARY_NAME"
cp "$SELF_DIR/launcher/launch.sh" "$RUNTIME/launch.sh"
cp "$SELF_DIR/launcher/autostart_ctl.sh" "$RUNTIME/autostart_ctl.sh"
cp "$SELF_DIR/launcher/autostart_launch.sh" "$RUNTIME/autostart_launch.sh"
cp "$SELF_DIR/launcher/config.json" "$RUNTIME/config.json"
cp "$SELF_DIR/input_map/capture-one.sh" "$RUNTIME/tools/capture-one.sh"
cp "$SELF_DIR/launcher/apply_filter.sh" "$RUNTIME/tools/apply_filter.sh"
cp "$SELF_DIR/launcher/apply_auto_cheats.sh" "$RUNTIME/tools/apply_auto_cheats.sh"
cp "$SELF_DIR/launcher/apply_game_overrides.sh" "$RUNTIME/tools/apply_game_overrides.sh"
cp "$SELF_DIR/launcher/game_volume.sh" "$RUNTIME/tools/game_volume.sh"
cp "$SELF_DIR/launcher/apply_recommended_controls.sh" "$RUNTIME/tools/apply_recommended_controls.sh"
cp "$SELF_DIR/launcher/apply_splash.sh" "$RUNTIME/tools/apply_splash.sh"
cp "$SELF_DIR/launcher/PegasusG by ROC.sh" "$STAGE_ROOT/Roms/APPS/${APP_ENTRY_NAME}.sh"
cp "$REPO_ROOT/assets/ui/pegasusg_app_entry.png" "$STAGE_ROOT/Roms/APPS/Imgs/${APP_ENTRY_NAME}.png"
cp "$REPO_ROOT/assets/ui/pegasus_g.png" "$RUNTIME/assets/ui/pegasus_g.png"
cp "$REPO_ROOT/assets/cheats/gba-auto-cheats.zip" "$RUNTIME/assets/cheats/gba-auto-cheats.zip"
cp "$REPO_ROOT/LICENSE.md" "$RUNTIME/LICENSE.md"
cp "$REPO_ROOT/NOTICE.md" "$RUNTIME/NOTICE.md"
cp "$REPO_ROOT/NOTICE.zh-CN.md" "$RUNTIME/NOTICE.zh-CN.md"
cp "$REPO_ROOT/THIRD_PARTY_NOTICES.md" "$RUNTIME/THIRD_PARTY_NOTICES.md"
cp "$REPO_ROOT/third_party/licenses/"* "$RUNTIME/third_party/licenses/"
cp "$SELF_DIR/core_options/mGBA/"*.opt "$RUNTIME/assets/core_options/mGBA/"
cp "$SELF_DIR/assets/splash/bootlogo.bmp" "$RUNTIME/assets/splash/bootlogo.bmp"
cp "$SELF_DIR/assets/splash/splash.png" "$RUNTIME/assets/splash/splash.png"
cp "$SELF_DIR/assets/splash/splash.jpg" "$RUNTIME/assets/splash/splash.jpg"
cp "$SELF_DIR/filter_injectors/calibrated/preset.glslp" "$RUNTIME/assets/filters/calibrated.glslp"
cp "$SELF_DIR/filter_injectors/original/preset.glslp" "$RUNTIME/assets/filters/original.glslp"
cp "$SELF_DIR/filter_injectors/common/shaders/"*.glsl "$RUNTIME/assets/filters/shaders/"
cp "$SELF_DIR/recommended_controls/global.cfg" "$RUNTIME/assets/recommended_controls/global.cfg"
cp "$SELF_DIR/recommended_controls/core/"*.cfg "$RUNTIME/assets/recommended_controls/core/"
cp "$SELF_DIR/recommended_controls/remaps/"*.rmp "$RUNTIME/assets/recommended_controls/remaps/"
for core in mgba_libretro.so gpsp_libretro.so vbam_libretro.so vba_next_libretro.so gpsp_rumble_libretro.so; do
  if [ ! -f "$REPO_ROOT/assets/cores/$core" ]; then
    echo "[h700] missing managed core asset: $core" >&2
    exit 1
  fi
  cp "$REPO_ROOT/assets/cores/$core" "$RUNTIME/assets/cores/$core"
done
if [ ! -d "$MUSIC_SOURCE" ]; then
  echo "[h700] missing Pegasus music directory: $MUSIC_SOURCE" >&2
  exit 1
fi
find "$MUSIC_SOURCE" -maxdepth 1 -type f -iname '*.mp3' -exec cp -p '{}' "$RUNTIME/assets/music/" ';'
music_count="$(find "$RUNTIME/assets/music" -maxdepth 1 -type f -iname '*.mp3' | wc -l)"
if [ "$music_count" -eq 0 ]; then
  echo "[h700] no Pegasus music tracks were staged" >&2
  exit 1
fi
echo "[h700] staged music tracks: $music_count"
printf '%s\n' "$VERSION" >"$RUNTIME/version.txt"

if [ -n "$FONT_SOURCE" ] && [ -f "$FONT_SOURCE" ]; then
  cp "$FONT_SOURCE" "$RUNTIME/assets/fonts/ui_font_02.ttf"
else
  echo "[h700] warning: no bundled font selected; package will rely on system font paths" >&2
fi

cp "$SELF_DIR/launcher/mods.txt" "$RUNTIME/config/mods.txt"

chmod +x "$RUNTIME/$BINARY_NAME" "$RUNTIME/launch.sh" "$RUNTIME/autostart_ctl.sh" \
  "$RUNTIME/autostart_launch.sh" "$RUNTIME/tools/capture-one.sh" \
  "$RUNTIME/tools/apply_filter.sh" \
  "$RUNTIME/tools/apply_auto_cheats.sh" \
  "$RUNTIME/tools/apply_game_overrides.sh" \
  "$RUNTIME/tools/game_volume.sh" \
  "$RUNTIME/tools/apply_recommended_controls.sh" \
  "$RUNTIME/tools/apply_splash.sh" \
  "$STAGE_ROOT/Roms/APPS/${APP_ENTRY_NAME}.sh"

if command -v "$READ_ELF" >/dev/null 2>&1; then
  "$READ_ELF" -h "$RUNTIME/$BINARY_NAME" >/dev/null
fi

python3 - "$STAGE_ROOT" "$ARCHIVE" "$OUTPUT" "$APP_ID" "$APP_ENTRY_NAME" "$BINARY_NAME" <<'PY'
import pathlib
import sys
import zipfile

stage = pathlib.Path(sys.argv[1])
archive = pathlib.Path(sys.argv[2])
output = sys.argv[3]
app_id = sys.argv[4]
app_entry_name = sys.argv[5]
binary_name = sys.argv[6]
runtime = stage / "Roms" / "APPS" / app_id
required = [
    stage / "Roms" / "APPS" / f"{app_entry_name}.sh",
    stage / "Roms" / "APPS" / "Imgs" / f"{app_entry_name}.png",
    runtime / binary_name,
    runtime / "launch.sh",
    runtime / "autostart_ctl.sh",
    runtime / "autostart_launch.sh",
    runtime / "config.json",
    runtime / "version.txt",
    runtime / "LICENSE.md",
    runtime / "NOTICE.md",
    runtime / "NOTICE.zh-CN.md",
    runtime / "THIRD_PARTY_NOTICES.md",
    runtime / "third_party" / "licenses" / "mGBA-MPL-2.0.txt",
    runtime / "third_party" / "licenses" / "gpSP-GPL-2.0.txt",
    runtime / "third_party" / "licenses" / "VBA-M-License.txt",
    runtime / "third_party" / "licenses" / "VBA-Next-GPL-2.0.txt",
    runtime / "tools" / "apply_filter.sh",
    runtime / "tools" / "apply_auto_cheats.sh",
    runtime / "tools" / "apply_game_overrides.sh",
    runtime / "tools" / "game_volume.sh",
    runtime / "tools" / "apply_recommended_controls.sh",
    runtime / "tools" / "apply_splash.sh",
    runtime / "assets" / "filters" / "calibrated.glslp",
    runtime / "assets" / "cheats" / "gba-auto-cheats.zip",
    runtime / "assets" / "core_options" / "mGBA" / "火焰纹章 埃力格之枪.opt",
    runtime / "assets" / "core_options" / "mGBA" / "口袋妖怪 龙珠Z 超激战.opt",
    runtime / "assets" / "filters" / "original.glslp",
    runtime / "assets" / "filters" / "shaders" / "image-adjustment.glsl",
    runtime / "assets" / "filters" / "shaders" / "nds-color.glsl",
    runtime / "assets" / "filters" / "shaders" / "retro-v2.glsl",
    runtime / "assets" / "recommended_controls" / "global.cfg",
    runtime / "assets" / "recommended_controls" / "core" / "mGBA.cfg",
    runtime / "assets" / "recommended_controls" / "core" / "gpSP.cfg",
    runtime / "assets" / "recommended_controls" / "core" / "VBA-M.cfg",
    runtime / "assets" / "recommended_controls" / "remaps" / "mGBA.rmp",
    runtime / "assets" / "recommended_controls" / "remaps" / "gpSP.rmp",
    runtime / "assets" / "recommended_controls" / "remaps" / "VBA-M.rmp",
    runtime / "assets" / "splash" / "bootlogo.bmp",
    runtime / "assets" / "splash" / "splash.png",
    runtime / "assets" / "splash" / "splash.jpg",
    runtime / "assets" / "cores" / "gpsp_rumble_libretro.so",
    runtime / "assets" / "cores" / "mgba_libretro.so",
    runtime / "assets" / "cores" / "gpsp_libretro.so",
    runtime / "assets" / "cores" / "vbam_libretro.so",
    runtime / "assets" / "cores" / "vba_next_libretro.so",
]
missing = [str(path) for path in required if not path.is_file()]
if missing:
    raise SystemExit("[h700] package validation missing: " + ", ".join(missing))
cheat_payload = runtime / "assets" / "cheats" / "gba-auto-cheats.zip"
with zipfile.ZipFile(cheat_payload) as cheat_zip:
    cheat_entries = [name for name in cheat_zip.namelist() if not name.endswith("/")]
expected_cores = {"gpSP", "mGBA", "VBA-M"}
actual_cores = {name.split("/", 1)[0] for name in cheat_entries if "/" in name}
if actual_cores != expected_cores:
    raise SystemExit(f"[h700] automatic cheat cores mismatch: {sorted(actual_cores)}")
if len(cheat_entries) != 1416 or any(not name.endswith(".cht") for name in cheat_entries):
    raise SystemExit(f"[h700] automatic cheat payload invalid: {len(cheat_entries)} files")
for core in sorted(expected_cores):
    count = sum(name.startswith(core + "/") for name in cheat_entries)
    if count != 472:
        raise SystemExit(f"[h700] automatic cheat count for {core}: {count}")
music_tracks = list((runtime / "assets" / "music").glob("*.mp3"))
if not music_tracks:
    raise SystemExit("[h700] package validation found no music tracks")
if output == "Zip":
    archive.parent.mkdir(parents=True, exist_ok=True)
    if archive.exists():
        archive.unlink()
    with zipfile.ZipFile(archive, "w", zipfile.ZIP_DEFLATED, compresslevel=6) as zf:
        for path in sorted(stage.rglob("*")):
            if path.is_file():
                zf.write(path, path.relative_to(stage).as_posix())
print(f"[h700] staged: {stage}")
if output == "Zip":
    print(f"[h700] archive: {archive}")
PY
