#include "gba_preferences.h"

#include <cassert>
#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;

int main() {
  const fs::path root = fs::temp_directory_path() / "roc_gba_preferences_test";
  const fs::path path = root / "preferences.txt";
  fs::remove_all(root);

  GbaPreferencesStore store(path.u8string());
  GbaPreferences preferences;
  assert(!store.Load(&preferences));
  assert(preferences.bgm_mode == GbaBgmMode::EightBit);
  assert(preferences.preview_video_loop);
  assert(preferences.grid_size == GbaGridSize::Large);
  assert(preferences.theme_color == GbaThemeColor::Black);
  assert(preferences.filter_mode == GbaFilterMode::Calibrated);
  assert(preferences.use_recommended_controls);
  assert(preferences.use_pegasus_splash);
  assert(preferences.cover_title_size_level == 3);
  assert(preferences.description_size_level == 4);
  assert(preferences.show_cover_titles);
  assert(!preferences.fullscreen_grid);
  assert(preferences.system_volume == 0);
  assert(!preferences.super_standby);

  preferences.bgm_mode = GbaBgmMode::EightBit;
  preferences.preview_video_loop = false;
  preferences.grid_size = GbaGridSize::Small;
  preferences.theme_color = GbaThemeColor::TransparentRed;
  preferences.filter_mode = GbaFilterMode::Custom;
  preferences.use_recommended_controls = false;
  preferences.use_pegasus_splash = false;
  preferences.cover_title_size_level = 3;
  preferences.description_size_level = 5;
  preferences.show_cover_titles = false;
  preferences.fullscreen_grid = true;
  preferences.system_volume = 8;
  preferences.super_standby = true;
  assert(store.Save(preferences));

  GbaPreferences loaded;
  assert(store.Load(&loaded));
  assert(loaded.bgm_mode == GbaBgmMode::EightBit);
  assert(!loaded.preview_video_loop);
  assert(loaded.grid_size == GbaGridSize::Small);
  assert(loaded.theme_color == GbaThemeColor::TransparentRed);
  assert(loaded.filter_mode == GbaFilterMode::Custom);
  assert(!loaded.use_recommended_controls);
  assert(!loaded.use_pegasus_splash);
  assert(loaded.cover_title_size_level == 3);
  assert(loaded.description_size_level == 5);
  assert(!loaded.show_cover_titles);
  assert(loaded.fullscreen_grid);
  assert(loaded.system_volume == 8);
  assert(loaded.super_standby);

  std::ofstream(path, std::ios::trunc) << "1 0 0\n";
  loaded.theme_color = GbaThemeColor::MetalBlue;
  loaded.super_standby = true;
  assert(store.Load(&loaded));
  assert(loaded.bgm_mode == GbaBgmMode::EightBit);
  assert(loaded.preview_video_loop);
  assert(!loaded.super_standby);
  assert(loaded.grid_size == GbaGridSize::Large);
  assert(loaded.theme_color == GbaThemeColor::Black);
  assert(loaded.filter_mode == GbaFilterMode::Calibrated);
  assert(loaded.use_recommended_controls);
  assert(loaded.use_pegasus_splash);
  assert(loaded.cover_title_size_level == 3);
  assert(loaded.description_size_level == 4);
  assert(loaded.show_cover_titles);
  assert(!loaded.fullscreen_grid);

  std::ofstream(path, std::ios::trunc) << "3 0 0 3 0 0\n";
  assert(store.Load(&loaded));
  assert(loaded.cover_title_size_level == 3);
  assert(loaded.description_size_level == 4);

  std::ofstream(path, std::ios::trunc) << "2 0 0 2\n";
  loaded.cover_title_size_level = 4;
  loaded.description_size_level = 4;
  assert(store.Load(&loaded));
  assert(loaded.theme_color == GbaThemeColor::MetalSilver);
  assert(loaded.cover_title_size_level == 3);
  assert(loaded.description_size_level == 4);

  std::ofstream(path, std::ios::trunc) << "2 0 0 8\n";
  assert(!store.Load(&loaded));
  std::ofstream(path, std::ios::trunc) << "4 0 0 3 6 0\n";
  assert(!store.Load(&loaded));
  std::ofstream(path, std::ios::trunc) << "5 0 0 3 3 4 2 0\n";
  assert(!store.Load(&loaded));
  std::ofstream(path, std::ios::trunc) << "6 0 0 3 3 4 1 0 2\n";
  assert(!store.Load(&loaded));
  std::ofstream(path, std::ios::trunc) << "6 0 0 3 3 4 1 0 1\n";
  assert(store.Load(&loaded));
  assert(loaded.theme_color == GbaThemeColor::Black);
  std::ofstream(path, std::ios::trunc) << "6 0 0 4 3 4 1 0 1\n";
  assert(!store.Load(&loaded));
  std::ofstream(path, std::ios::trunc) << "7 0 0 11 3 4 1 0 1\n";
  assert(!store.Load(&loaded));
  std::ofstream(path, std::ios::trunc) << "7 0 0 3 3 4 1 0 1\n";
  loaded.filter_mode = GbaFilterMode::Custom;
  assert(store.Load(&loaded));
  assert(loaded.filter_mode == GbaFilterMode::Calibrated);
  std::ofstream(path, std::ios::trunc) << "8 0 0 3 3 4 1 0 1 3\n";
  assert(!store.Load(&loaded));
  std::ofstream(path, std::ios::trunc) << "8 0 0 3 3 4 1 0 1 2\n";
  loaded.use_pegasus_splash = false;
  assert(store.Load(&loaded));
  assert(loaded.use_pegasus_splash);
  std::ofstream(path, std::ios::trunc) << "9 0 0 3 3 4 1 0 1 0 2\n";
  assert(!store.Load(&loaded));
  std::ofstream(path, std::ios::trunc) << "10 0 0 3 3 4 1 0 1 0 1 2\n";
  assert(!store.Load(&loaded));
  std::ofstream(path, std::ios::trunc) << "10 0 0 3 3 4 1 0 1 0 1 1\n";
  loaded.system_volume = 7;
  assert(store.Load(&loaded));
  assert(loaded.system_volume == 0);
  std::ofstream(path, std::ios::trunc) << "11 0 0 3 3 4 1 0 1 0 1 1 10\n";
  assert(!store.Load(&loaded));
  std::ofstream(path, std::ios::trunc) << "11 0 0 3 3 4 1 0 1 0 1 1 8\n";
  loaded.super_standby = true;
  assert(store.Load(&loaded) && !loaded.super_standby && loaded.system_volume == 8);
  std::ofstream(path, std::ios::trunc) << "12 0 0 3 3 4 1 0 1 0 1 1 8 2\n";
  assert(!store.Load(&loaded));
  std::ofstream(path, std::ios::trunc) << "12 0 0 3 3 4 1 0 1 0 1 1 8\n";
  assert(!store.Load(&loaded));
  fs::remove_all(root);
  return 0;
}
