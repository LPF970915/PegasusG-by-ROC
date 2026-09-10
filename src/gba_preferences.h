#pragma once

#include <string>

enum class GbaBgmMode {
  EightBit = 0,
  GameAudio = 1,
  Silent = 2,
};

enum class GbaGridSize {
  Large = 0,
  Medium = 1,
  Small = 2,
};

enum class GbaFilterMode {
  Calibrated = 0,
  Original = 1,
  Custom = 2,
};

enum class GbaThemeColor {
  MetalBlue = 0,
  MetalPink = 1,
  MetalSilver = 2,
  Black = 3,
  Indigo = 4,
  Yellow = 5,
  MetalDeepBlue = 6,
  GlacierBlue = 7,
  TransparentGreen = 8,
  Gray = 9,
  TransparentRed = 10,
};

constexpr int kGbaThemeColorCount = 11;

struct GbaPreferences {
  GbaBgmMode bgm_mode = GbaBgmMode::EightBit;
  bool preview_video_loop = true;
  GbaGridSize grid_size = GbaGridSize::Large;
  GbaThemeColor theme_color = GbaThemeColor::Black;
  GbaFilterMode filter_mode = GbaFilterMode::Calibrated;
  bool use_recommended_controls = true;
  bool use_pegasus_splash = true;
  int cover_title_size_level = 3;
  int description_size_level = 4;
  bool show_cover_titles = true;
  bool fullscreen_grid = false;
  int system_volume = 0;
  bool super_standby = false;
};

class GbaPreferencesStore {
 public:
  explicit GbaPreferencesStore(std::string path);

  bool Load(GbaPreferences *preferences) const;
  bool Save(const GbaPreferences &preferences) const;

 private:
  std::string path_;
};
