# PegasusG by ROC - H700 Build and Package

This directory builds the AArch64 application package for a stock H700 APPS
launcher. It uses the target firmware's SDL2, SDL2_image, SDL2_ttf and audio
libraries; it does not carry a second general-purpose runtime.

## Build

```powershell
.\H700\build_app.ps1 -Output Stage -Version 1.08 -Sysroot 'D:\path\to\sysroot'
.\H700\build_app.ps1 -Output Zip -Version 1.08 -Sysroot 'D:\path\to\sysroot'
```

The sysroot may instead be placed at `H700\sysroot\` (ignored by Git). The
default music source is `assets\music\builtin\`, which contains the selected
11-track set. Override it with `-MusicSource` when building with a complete,
separately licensed collection.

The Docker builder uses the same staging and package flow:

```powershell
.\H700\build_app.ps1 -Builder Docker -Distro Ubuntu-24.04 -Output Stage -Version 1.08
.\H700\build_app.ps1 -SkipBuild -Distro Ubuntu-24.04 -Output Zip -Version 1.08
```

See the [root README](../README.md#h700-cross-build) for Docker/WSL setup.
`-MusicSource`, `-FontSource`, `-Version` and `-Output` work with both builders.
The shell equivalents are `PEGASUSG_BUILDER=Docker` and
`PEGASUSG_SKIP_BUILD=1`; `make packzip` uses the latter with `PEGASUSG_OUTPUT=Zip`.

## Split Packages

After a staging build:

```powershell
.\H700\build_split_packages.ps1 -Version 1.08 -MainOnly
.\H700\build_split_packages.ps1 -Version 1.08 `
  -FullMusicSource 'D:\path\to\complete\Music'
```

The first command creates the small main package. The second additionally
creates the version-independent complete music supplement and expects 324 MP3
tracks. Archives are written under `H700\Downloads\`.

## Package Layout

```text
Roms/APPS/PegasusG by ROC.sh
Roms/APPS/PegasusG by ROC/
  pegasusg_by_roc
  launch.sh
  autostart_ctl.sh
  autostart_launch.sh
  tools/
  assets/cores/
  assets/filters/
  assets/recommended_controls/
  assets/splash/
  LICENSE.md
  NOTICE.md
  NOTICE.zh-CN.md
  THIRD_PARTY_NOTICES.md
  third_party/licenses/
```

## Content Roots

The launcher supports GBA, GBA hack and GBA vib collections on both card
mounts. Set `PEGASUSG_CONTENT_ROOTS` to override the default roots. ROMs and
commercial content are not included in this source repository.

## H700 Artwork

When the runtime identifies an H700 device, `use_mini_assets` selects the
copyright-bearing `mini_boxfront.png/.jpg` and `mini_logo.png/.jpg` files next
to the original media. Desktop and Android paths do not read these optimized
assets. Legacy `.bmp` thumbnail names are no longer part of the runtime
contract.

## Launcher Operations

```sh
"/mnt/mmc/Roms/APPS/PegasusG by ROC/autostart_ctl.sh" enable
"/mnt/mmc/Roms/APPS/PegasusG by ROC/autostart_ctl.sh" disable
"/mnt/mmc/Roms/APPS/PegasusG by ROC/autostart_ctl.sh" uninstall
```

The launcher stores state and logs under `/mnt/data/pegasusg-by-roc/`. See
`docs/PORTING_GUIDE.md` for adapting mount points, input mappings, power
services and splash targets to another H700-class machine.

## Controls

| Button | Main screen | Search keyboard |
| --- | --- | --- |
| D-pad | Select a game | Select a key |
| A | Launch / select | Activate the selected key |
| B / Menu | Settings, Help, Exit | Cancel without applying |
| X | Full-screen grid | Backspace; hold for faster repeat |
| Y | Quick settings | Space |
| L1 / R1 | Previous / next category | L1 changes case |
| L2 | Next theme colour | |
| R2 | Search | |
| Select | Toggle favourite | Clear input |
| Start | Select game core | Apply search |

Quick settings share their values with Settings. L2 also changes the theme
while this menu is open. Exit offers Return to system, Restart, Shut down and Cancel;
Cancel is selected initially; Shut down is highlighted red when selected. Favourites display a heart even when cover
titles are hidden. Favourites appear first, earliest added first, and the
selected game remains selected after adding or removing a favourite. Removing
one from the Favourites category returns to its native category. Older saved
favourites without ordering metadata retain their original relative order.
The full-screen grid does not play preview video.

Lid sleep uses the same launcher-managed process restart as the power button.
SDL is destroyed before suspension; waking starts a fresh frontend and restores
the selected game and category, brightness and input devices. Settings includes a Standby Mode option. Default Standby (the default) sets
`os_sleep` to 0 for lid sleep, allowing hall wake. Super Standby uses 16 and
requires the power button to wake. The power button also uses Super Standby (16),
which requires the power button to wake and consumes less standby power.
Update `launch.sh` together with the executable for this behavior.

Descriptions scroll automatically and pause while a menu or search is open.
The right-aligned hints stay within the grid area; A/B hints are omitted first
when space is limited. Active search text has its own background.

Search filters the current category and remains active across category changes.
`口袋妖怪`, `kou dai yao guai` and `kdyg` all match 口袋妖怪. Use `v` for ü.
The bundled table uses each character's first reading, not a phrase dictionary.
On a desktop, type using the system keyboard/IME; Enter applies, Backspace
deletes and Escape cancels. `PEGASUSG_FONT` selects a font for preview screenshots.

Frontend Volume controls frontend playback. System Volume offers Sync
(the default) or a fixed level from 1 to 9. Sync inherits the frontend volume,
including mute, when starting a game. Fixed levels remain independent of the
frontend setting. The launch request carries this setting to `launch.sh` and
`tools/game_volume.sh`; update both scripts along with the executable.
