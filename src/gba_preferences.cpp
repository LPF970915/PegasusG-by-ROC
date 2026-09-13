#include "gba_preferences.h"

#include <filesystem>
#include <fstream>
#include <utility>

namespace fs = std::filesystem;

GbaPreferencesStore::GbaPreferencesStore(std::string path) : path_(std::move(path)) {}

bool GbaPreferencesStore::Load(GbaPreferences *preferences) const {
  if (!preferences) return false;
  std::ifstream input(fs::u8path(path_));
  int version = 0;
  int bgm_mode = 0;
  int grid_size = 0;
  int theme_color = static_cast<int>(GbaThemeColor::Black);
  int cover_title_size_level = 3;
  int description_size_level = 4;
  int show_cover_titles = 1;
  int fullscreen_grid = 0;
  int preview_video_loop = 1;
  int filter_mode = static_cast<int>(GbaFilterMode::Calibrated);
  int use_pegasus_splash = 1;
  int use_recommended_controls = 1;
  int system_volume = 0;
  int super_standby = 0;
  int maximum_theme_color = 3;
  if (!(input >> version >> bgm_mode >> grid_size)) return false;
  if (version == 10 || version == 11 || version == 12) {
    maximum_theme_color = kGbaThemeColorCount - 1;
    if (!(input >> theme_color >> cover_title_size_level >> description_size_level >>
          show_cover_titles >> fullscreen_grid >> preview_video_loop >> filter_mode >>
          use_pegasus_splash >> use_recommended_controls)) {
      return false;
    }
  } else if (version == 9) {
    maximum_theme_color = kGbaThemeColorCount - 1;
    if (!(input >> theme_color >> cover_title_size_level >> description_size_level >>
          show_cover_titles >> fullscreen_grid >> preview_video_loop >> filter_mode >>
          use_pegasus_splash)) {
      return false;
    }
  } else if (version == 8) {
    maximum_theme_color = kGbaThemeColorCount - 1;
    if (!(input >> theme_color >> cover_title_size_level >> description_size_level >>
          show_cover_titles >> fullscreen_grid >> preview_video_loop >> filter_mode)) {
      return false;
    }
  } else if (version == 7) {
    maximum_theme_color = kGbaThemeColorCount - 1;
    if (!(input >> theme_color >> cover_title_size_level >> description_size_level >>
          show_cover_titles >> fullscreen_grid >> preview_video_loop)) {
      return false;
    }
  } else if (version == 6) {
    if (!(input >> theme_color >> cover_title_size_level >> description_size_level >>
          show_cover_titles >> fullscreen_grid >> preview_video_loop)) {
      return false;
    }
  } else if (version == 5) {
    if (!(input >> theme_color >> cover_title_size_level >> description_size_level >>
          show_cover_titles >> fullscreen_grid)) {
      return false;
    }
  } else if (version == 4) {
    if (!(input >> theme_color >> cover_title_size_level >> description_size_level)) {
      return false;
    }
  } else if (version == 3) {
    if (!(input >> theme_color >> cover_title_size_level >> description_size_level)) {
      return false;
    }
    if (cover_title_size_level == 0 && description_size_level == 0) {
      cover_title_size_level = 3;
      description_size_level = 4;
    }
  } else if (version == 2) {
    if (!(input >> theme_color)) return false;
  } else if (version != 1) {
    return false;
  }
  if (version >= 11 && !(input >> system_volume)) return false;
  if (version == 12 && !(input >> super_standby)) return false;
  if (bgm_mode < 0 || bgm_mode > 2 || grid_size < 0 || grid_size > 2 ||
      theme_color < 0 || theme_color > maximum_theme_color ||
      cover_title_size_level < 0 || cover_title_size_level > 5 ||
      description_size_level < 0 || description_size_level > 5 ||
      system_volume < 0 || system_volume > 9 ||
      super_standby < 0 || super_standby > 1 ||
      show_cover_titles < 0 || show_cover_titles > 1 ||
      fullscreen_grid < 0 || fullscreen_grid > 1 ||
      preview_video_loop < 0 || preview_video_loop > 1 ||
      filter_mode < 0 || filter_mode > 2 ||
      use_recommended_controls < 0 || use_recommended_controls > 1 ||
      use_pegasus_splash < 0 || use_pegasus_splash > 1) {
    return false;
  }
  preferences->system_volume = system_volume;
  preferences->super_standby = super_standby != 0;
  preferences->bgm_mode = static_cast<GbaBgmMode>(bgm_mode);
  preferences->grid_size = static_cast<GbaGridSize>(grid_size);
  preferences->theme_color = static_cast<GbaThemeColor>(theme_color);
  preferences->cover_title_size_level = cover_title_size_level;
  preferences->description_size_level = description_size_level;
  preferences->show_cover_titles = show_cover_titles != 0;
  preferences->fullscreen_grid = fullscreen_grid != 0;
  preferences->preview_video_loop = preview_video_loop != 0;
  preferences->filter_mode = static_cast<GbaFilterMode>(filter_mode);
  preferences->use_recommended_controls = use_recommended_controls != 0;
  preferences->use_pegasus_splash = use_pegasus_splash != 0;
  return true;
}

bool GbaPreferencesStore::Save(const GbaPreferences &preferences) const {
  const fs::path path = fs::u8path(path_);
  std::error_code error;
  fs::create_directories(path.parent_path(), error);
  const fs::path temporary = path.string() + ".tmp";
  {
    std::ofstream output(temporary, std::ios::trunc);
    if (!output) return false;
    output << 12 << ' ' << static_cast<int>(preferences.bgm_mode) << ' '
           << static_cast<int>(preferences.grid_size) << ' '
           << static_cast<int>(preferences.theme_color) << ' '
           << preferences.cover_title_size_level << ' '
           << preferences.description_size_level << ' '
           << (preferences.show_cover_titles ? 1 : 0) << ' '
           << (preferences.fullscreen_grid ? 1 : 0) << ' '
           << (preferences.preview_video_loop ? 1 : 0) << ' '
           << static_cast<int>(preferences.filter_mode) << ' '
           << (preferences.use_pegasus_splash ? 1 : 0) << ' '
           << (preferences.use_recommended_controls ? 1 : 0) << ' '
           << preferences.system_volume << ' ' << (preferences.super_standby ? 1 : 0) << '\n';
    if (!output) return false;
  }
  fs::rename(temporary, path, error);
  if (!error) return true;
  fs::remove(path, error);
  error.clear();
  fs::rename(temporary, path, error);
  return !error;
}
