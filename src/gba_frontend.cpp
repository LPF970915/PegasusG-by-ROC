#include "gba_frontend.h"

#include <SDL_image.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <random>
#include <sstream>

#ifndef _WIN32
#include <fcntl.h>
#include <linux/input.h>
#include <sys/ioctl.h>
#include <unistd.h>
#endif

#include "pegasus_metadata.h"
#include "optimized_image_path.h"
#include "game_search.h"
#include "ui_icons_bitmap.h"

namespace fs = std::filesystem;

namespace {

constexpr int kExitLaunchGame = 20;
constexpr int kExitSuspendManual = 21;
constexpr int kExitSuspendAutomatic = 22;
constexpr int kExitRestartSystem = 23;
constexpr int kExitPowerOffSystem = 24;
constexpr int kExitSuspendAutomaticDeep = 25;
constexpr size_t kImageCacheCapacity = 64;
constexpr int kTabRecent = 0;
constexpr int kTabGba = 1;
constexpr int kTabMod = 2;
constexpr int kTabRumble = 3;
constexpr int kTabFavorite = 4;
constexpr int kTabCount = 5;
constexpr Uint32 kCoverScaleDuration = 150;
constexpr Uint32 kTabTransitionDuration = 240;
constexpr Uint32 kChromeTransitionDuration = 300;
constexpr Uint32 kPrewarmIdleDelay = 48;
constexpr Uint32 kGridRepeatInitialDelay = 380;
constexpr Uint32 kGridRepeatInterval = 210;
constexpr Uint32 kDescriptionRepeatInitialDelay = 360;
constexpr Uint32 kDescriptionRepeatInterval = 90;
constexpr Uint32 kCoverTitleMarqueeDelay = 900;
constexpr float kCoverTitleMarqueeSpeed = 28.0f;
constexpr int kCoverTitleMarqueeGap = 24;
constexpr std::size_t kMaximumRecentGames = 100;
constexpr int kVersionMenuVisibleRows = 6;
constexpr int kSettingsCount = 17;
constexpr int kQuickSettings[] = {-1, 3, 4, 5, 8, 9, 10, 11, 12, 13, 17, 6};
constexpr int kQuickSettingsCount = sizeof(kQuickSettings) / sizeof(kQuickSettings[0]);
constexpr int kGridX = 240;
constexpr int kGridY = 45;
constexpr int kGridInset = 16;
constexpr int kFullscreenGridInset = 18;
constexpr int kCoverTitleBaseFontSize = 12;
constexpr int kDescriptionBaseFontSize = 14;
constexpr int kFontSizeLevelCount = 6;
constexpr int kDescriptionTextHeight = 171;
constexpr const char *kTabs[kTabCount] = {
    "最近游戏", "GBA", "GBA改版", "GBA震动", "收藏",
};
constexpr int kTabWidths[kTabCount] = {112, 78, 110, 106, 82};

constexpr SDL_Color kBackground{3, 4, 5, 255};
constexpr SDL_Color kPanel{8, 9, 10, 255};
constexpr SDL_Color kInk{242, 243, 245, 255};
constexpr SDL_Color kMuted{177, 181, 186, 255};
constexpr SDL_Color kAccent{246, 197, 73, 255};

std::string SampledFileFingerprint(const std::string &path) {
  if (path.empty()) return {};
  std::error_code error;
  const std::uintmax_t size = fs::file_size(fs::u8path(path), error);
  if (error) return {};
  std::ifstream input(fs::u8path(path), std::ios::binary);
  if (!input) return {};
  constexpr std::size_t kSampleBytes = 2048;
  std::array<char, kSampleBytes> sample{};
  std::uint64_t hash = 1469598103934665603ULL;
  const auto mix = [&](unsigned char value) {
    hash ^= value;
    hash *= 1099511628211ULL;
  };
  for (int shift = 0; shift < 8; ++shift) {
    mix(static_cast<unsigned char>((size >> (shift * 8)) & 0xff));
  }
  const std::uintmax_t last_offset = size > kSampleBytes ? size - kSampleBytes : 0;
  const std::uintmax_t offsets[] = {0, size / 2, last_offset};
  for (const std::uintmax_t offset : offsets) {
    input.clear();
    input.seekg(static_cast<std::streamoff>(offset));
    input.read(sample.data(), sample.size());
    const std::streamsize count = input.gcount();
    for (std::streamsize index = 0; index < count; ++index) {
      mix(static_cast<unsigned char>(sample[static_cast<std::size_t>(index)]));
    }
  }
  std::ostringstream result;
  result << std::hex << size << ':' << hash;
  return result.str();
}

bool SameSampledFile(const std::string &left_path, const std::string &right_path) {
  if (left_path.empty() || right_path.empty()) return false;
  if (left_path == right_path) return true;
  const std::string left = SampledFileFingerprint(left_path);
  return !left.empty() && left == SampledFileFingerprint(right_path);
}

struct ThemeColors {
  SDL_Color bar;
  SDL_Color selected;
};

ThemeColors ColorsForTheme(GbaThemeColor theme) {
  switch (theme) {
    case GbaThemeColor::MetalBlue:
      return {{28, 48, 62, 255}, {55, 84, 102, 255}};
    case GbaThemeColor::MetalPink:
      return {{61, 38, 50, 255}, {94, 59, 76, 255}};
    case GbaThemeColor::MetalSilver:
      return {{57, 60, 64, 255}, {88, 92, 97, 255}};
    case GbaThemeColor::Black:
      return {{17, 18, 20, 255}, {47, 50, 54, 255}};
    case GbaThemeColor::Indigo:
      return {{50, 44, 92, 255}, {78, 69, 132, 255}};
    case GbaThemeColor::Yellow:
      return {{96, 72, 6, 255}, {139, 104, 12, 255}};
    case GbaThemeColor::MetalDeepBlue:
      return {{15, 25, 82, 255}, {30, 46, 124, 255}};
    case GbaThemeColor::GlacierBlue:
      return {{46, 61, 70, 255}, {72, 91, 103, 255}};
    case GbaThemeColor::TransparentGreen:
      return {{5, 70, 41, 255}, {10, 104, 60, 255}};
    case GbaThemeColor::Gray:
      return {{72, 74, 77, 255}, {103, 106, 110, 255}};
    case GbaThemeColor::TransparentRed:
      return {{91, 21, 28, 255}, {133, 35, 43, 255}};
  }
  return {{17, 18, 20, 255}, {47, 50, 54, 255}};
}

void Fill(SDL_Renderer *renderer, const SDL_Rect &rect, SDL_Color color) {
  SDL_SetRenderDrawColor(renderer, color.r, color.g, color.b, color.a);
  SDL_RenderFillRect(renderer, &rect);
}

void Stroke(SDL_Renderer *renderer, const SDL_Rect &rect, SDL_Color color) {
  SDL_SetRenderDrawColor(renderer, color.r, color.g, color.b, color.a);
  SDL_RenderDrawRect(renderer, &rect);
}

void FillRightSlantTab(SDL_Renderer *renderer, int x, int y, int width, int height,
                       int skew, SDL_Color color) {
  const SDL_Vertex vertices[] = {
      {{static_cast<float>(x), static_cast<float>(y)}, color, {0.0f, 0.0f}},
      {{static_cast<float>(x + width + skew), static_cast<float>(y)}, color, {0.0f, 0.0f}},
      {{static_cast<float>(x + width - skew), static_cast<float>(y + height)}, color, {0.0f, 0.0f}},
      {{static_cast<float>(x), static_cast<float>(y + height)}, color, {0.0f, 0.0f}},
  };
  constexpr int indices[] = {0, 1, 2, 0, 2, 3};
  SDL_RenderGeometry(renderer, nullptr, vertices, 4, indices, 6);
}

float InOutQuad(float value) {
  const float progress = std::clamp(value, 0.0f, 1.0f);
  return progress < 0.5f
      ? 2.0f * progress * progress
      : 1.0f - std::pow(-2.0f * progress + 2.0f, 2.0f) / 2.0f;
}

float OutCubic(float value) {
  const float progress = std::clamp(value, 0.0f, 1.0f);
  return 1.0f - std::pow(1.0f - progress, 3.0f);
}

float AnimationProgress(Uint32 started_at, Uint32 duration, Uint32 now) {
  if (duration == 0) return 1.0f;
  return std::min(1.0f, static_cast<float>(now - started_at) /
                            static_cast<float>(duration));
}

SDL_Color WithOpacity(SDL_Color color, float opacity) {
  color.a = static_cast<Uint8>(std::lround(
      static_cast<float>(color.a) * std::clamp(opacity, 0.0f, 1.0f)));
  return color;
}

int WrappedTab(int index) {
  return (index % kTabCount + kTabCount) % kTabCount;
}

std::string WrappedTextCacheKey(const std::string &text, int width, int size) {
  return std::to_string(width) + ":" + std::to_string(size) + ":" + text;
}

std::vector<std::string> Utf8Characters(const std::string &text) {
  std::vector<std::string> result;
  for (size_t i = 0; i < text.size();) {
    const unsigned char first = static_cast<unsigned char>(text[i]);
    size_t length = first < 0x80 ? 1 : (first < 0xe0 ? 2 : (first < 0xf0 ? 3 : 4));
    length = std::min(length, text.size() - i);
    result.push_back(text.substr(i, length));
    i += length;
  }
  return result;
}

std::string ClockText() {
  const auto now = std::chrono::system_clock::now();
  const std::time_t value = std::chrono::system_clock::to_time_t(now);
  std::tm local{};
#ifdef _WIN32
  localtime_s(&local, &value);
#else
  localtime_r(&value, &local);
#endif
  std::ostringstream out;
  out << std::put_time(&local, "%H:%M");
  return out.str();
}

std::string PaddedNumber(int value) {
  std::ostringstream out;
  out << std::setw(3) << std::setfill('0') << std::max(0, value);
  return out.str();
}

std::string FirstExisting(const std::vector<std::string> &paths) {
  for (const std::string &path : paths) {
    std::error_code error;
    if (fs::is_regular_file(fs::u8path(path), error)) return path;
  }
  return {};
}

}  // namespace

GbaFrontend::GbaFrontend(GbaFrontendOptions options)
    : options_(std::move(options)),
      state_((fs::u8path(options_.state_dir) / "games.tsv").u8string()),
      ui_state_((fs::u8path(options_.state_dir) / "ui_state.txt").u8string()),
      preferences_store_((fs::u8path(options_.state_dir) / "preferences.txt").u8string()),
      services_(options_.app_dir, options_.state_dir) {}

GbaFrontend::~GbaFrontend() {
  DestroyRuntime();
}

void GbaFrontend::DestroyRuntime() {
  video_.ReleaseDevices();
  if (icons_texture_) { SDL_DestroyTexture(icons_texture_); icons_texture_ = nullptr; }
#ifndef _WIN32
  for (int fd : evdev_input_fds_) close(fd);
#endif
  evdev_input_fds_.clear();
  has_evdev_gamepad_ = false;
  if (video_texture_) {
    SDL_DestroyTexture(video_texture_);
    video_texture_ = nullptr;
  }
  for (auto &[path, image] : images_) {
    (void)path;
    SDL_DestroyTexture(image.texture);
  }
  images_.clear();
  image_aliases_.clear();
  image_fingerprints_.clear();
  image_lru_.clear();
  for (auto &[key, value] : text_cache_) {
    (void)key;
    SDL_DestroyTexture(value.texture);
  }
  text_cache_.clear();
  for (auto &[size, font] : fonts_) {
    (void)size;
    TTF_CloseFont(font);
  }
  fonts_.clear();
  if (controller_) {
    SDL_GameControllerClose(controller_);
    controller_ = nullptr;
  }
  if (joystick_) {
    SDL_JoystickClose(joystick_);
    joystick_ = nullptr;
  }
  if (renderer_) {
    SDL_DestroyRenderer(renderer_);
    renderer_ = nullptr;
  }
  if (window_) {
    SDL_DestroyWindow(window_);
    window_ = nullptr;
  }
  if (initialized_) {
    TTF_Quit();
    IMG_Quit();
    SDL_Quit();
    initialized_ = false;
  }
  selected_video_path_.clear();
  video_version_ = 0;
  video_start_at_ = 0;
  held_grid_action_ = Action::None;
  held_description_action_ = Action::None;
  axis_x_ = 0;
  axis_y_ = 0;
  menu_button_held_ = false;
  menu_chord_used_ = false;
}

bool GbaFrontend::InitializeRuntime() {
  SDL_SetMainReady();
  if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMECONTROLLER | SDL_INIT_JOYSTICK) != 0) {
    std::cerr << "SDL_Init failed: " << SDL_GetError() << '\n';
    return false;
  }
  if ((IMG_Init(IMG_INIT_PNG | IMG_INIT_JPG) & IMG_INIT_PNG) == 0 || TTF_Init() != 0) {
    std::cerr << "SDL image/font initialization failed\n";
    SDL_Quit();
    return false;
  }
  initialized_ = true;
  if (options_.use_mini_assets && options_.screenshot_path.empty()) {
    SDL_DisplayMode mode{};
    if (SDL_GetCurrentDisplayMode(0, &mode) == 0 && mode.w > 0 && mode.h > 0) {
      options_.width = mode.w;
      options_.height = mode.h;
    }
  }
  const Uint32 flags = SDL_WINDOW_ALLOW_HIGHDPI |
                       (options_.screenshot_path.empty() ? SDL_WINDOW_SHOWN : SDL_WINDOW_HIDDEN);
  window_ = SDL_CreateWindow("PegasusG by ROC", SDL_WINDOWPOS_CENTERED,
                             SDL_WINDOWPOS_CENTERED, options_.width, options_.height, flags);
  if (!window_) {
    DestroyRuntime();
    return false;
  }
  renderer_ = SDL_CreateRenderer(window_, -1, options_.screenshot_path.empty()
      ? SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC : SDL_RENDERER_SOFTWARE);
  if (!renderer_) renderer_ = SDL_CreateRenderer(window_, -1, SDL_RENDERER_SOFTWARE);
  if (!renderer_) {
    DestroyRuntime();
    return false;
  }
  SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_BLEND);
  int output_width = options_.width, output_height = options_.height;
  SDL_GetRendererOutputSize(renderer_, &output_width, &output_height);
  canvas_width_ = output_width;
  canvas_height_ = output_height;
  SDL_RenderSetLogicalSize(renderer_, canvas_width_, canvas_height_);

  if (options_.font_path.empty()) {
    options_.font_path = FirstExisting({
        (fs::u8path(options_.app_dir) / "assets/fonts/ui_font_02.ttf").u8string(),
        "assets/fonts/ui_font_02.ttf", "C:/Windows/Fonts/msyh.ttc",
        "/mnt/vendor/bin/default.ttf", "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
    });
  }
  if (options_.font_path.empty() || !Font(16)) {
    DestroyRuntime();
    return false;
  }

  for (int index = 0; index < SDL_NumJoysticks(); ++index) {
    if (SDL_IsGameController(index)) {
      controller_ = SDL_GameControllerOpen(index);
      if (controller_) break;
    }
  }
  if (!controller_ && SDL_NumJoysticks() > 0) joystick_ = SDL_JoystickOpen(0);
  OpenEvdevInput();

  if (options_.screenshot_path.empty() && !services_.RestoreBrightness() &&
      options_.diagnostics) {
    std::cerr << "[gba] failed to restore panel brightness\n";
  }
  if (options_.screenshot_path.empty() && !services_.RestoreVolume() &&
      options_.diagnostics) {
    std::cerr << "[gba] failed to restore saved volume\n";
  }
  return true;
}

bool GbaFrontend::Initialize() {
  startup_started_ = std::chrono::steady_clock::now();
  std::ifstream version_file(fs::u8path(options_.app_dir) / "version.txt");
  std::string installed_version;
  if (version_file >> installed_version && installed_version.size() <= 16) {
    app_version_ = installed_version;
  }
  scan_report_ = ScanPegasusGbaRoots(options_.content_roots, options_.mod_overrides_path);
  const auto scan_finished = std::chrono::steady_clock::now();
  games_ = std::move(scan_report_.games);
  search_keys_.reserve(games_.size());
  for (const auto &game : games_) search_keys_.push_back(game_search::Key(game.title));
  state_.Load(&games_);
  if (state_.LimitRecent(&games_, kMaximumRecentGames)) state_.Save(games_);
  preferences_store_.Load(&preferences_);
  if (options_.screenshot_path.empty() && preferences_.use_recommended_controls &&
      !services_.SetRecommendedControls(true)) {
    preferences_.use_recommended_controls = false;
    preferences_store_.Save(preferences_);
    if (options_.diagnostics) {
      std::cerr << "[gba] failed to apply recommended RetroArch controls\n";
    }
  }
  if (options_.screenshot_path.empty() && preferences_.use_pegasus_splash &&
      !services_.SetPegasusSplash(true)) {
    preferences_.use_pegasus_splash = false;
    preferences_store_.Save(preferences_);
    if (options_.diagnostics) {
      std::cerr << "[gba] failed to apply PegasusG splash screens\n";
    }
  }
  chrome_animation_from_ = preferences_.fullscreen_grid ? 1.0f : 0.0f;
  chrome_animation_to_ = chrome_animation_from_;
  if (preferences_.bgm_mode == GbaBgmMode::EightBit) SelectEightBitTrack();
  next_recent_order_ = state_.NextRecentOrder(games_);
  if (options_.restore_ui) {
    RestoreUiState();
  } else {
    active_tab_ = kTabGba;
    selected_ = 0;
    scroll_row_ = 0;
    settings_open_ = false;
    core_menu_open_ = false;
    version_menu_open_ = false;
    restored_game_id_.clear();
    restored_scroll_row_ = -1;
  }

  RefreshVisible();
  if (!restored_game_id_.empty()) {
    const auto found = std::find_if(visible_.begin(), visible_.end(), [&](int index) {
      return games_[index].id == restored_game_id_;
    });
    if (found != visible_.end()) selected_ = static_cast<int>(found - visible_.begin());
  }
  if (restored_scroll_row_ >= 0) {
    scroll_row_ = restored_scroll_row_;
    EnsureSelectionVisible();
  }
  if (!InitializeRuntime()) return false;
  PollStatus();
  hall_state_ = services_.HallState();
  SelectionChanged();
  if (options_.diagnostics) {
    const auto initialized = std::chrono::steady_clock::now();
    int standard_games = 0;
    int mod_games = 0;
    int rumble_games = 0;
    int default_gpsp_games = 0;
    int multi_rom_games = 0;
    for (const GbaGame &game : games_) {
      if (game.is_rumble) ++rumble_games;
      else if (game.is_mod) ++mod_games;
      else ++standard_games;
      if (game.default_core == GbaCore::Gpsp) ++default_gpsp_games;
      if (!game.alternate_roms.empty()) ++multi_rom_games;
    }
    std::cerr << "[gba] metadata=" << scan_report_.metadata_files
              << " parsed=" << scan_report_.parsed_entries
              << " available=" << games_.size()
              << " missing=" << scan_report_.missing_rom_entries
              << " duplicates=" << scan_report_.duplicate_rom_entries
              << " unreferenced=" << scan_report_.unreferenced_roms << '\n';
    std::cerr << "[gba] categories gba=" << standard_games
              << " mod=" << mod_games << " rumble=" << rumble_games << '\n';
    std::cerr << "[gba] defaults gpsp=" << default_gpsp_games
              << " multi_rom=" << multi_rom_games << '\n';
    std::cerr << "[timing] scan_ms="
              << std::chrono::duration_cast<std::chrono::milliseconds>(
                     scan_finished - startup_started_).count()
              << " initialize_ms="
              << std::chrono::duration_cast<std::chrono::milliseconds>(
                     initialized - startup_started_).count() << '\n';
  }
  return true;
}

int GbaFrontend::Run() {
  int frames = 0;
  bool screenshot_action_sent = false;
  Uint32 screenshot_started_at = SDL_GetTicks();
  while (running_) {
    const Uint32 frame_started = SDL_GetTicks();
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
      if (event.type == SDL_QUIT) running_ = false;
      if (search_open_ && event.type == SDL_TEXTINPUT) {
        if (search_draft_.size() + std::string(event.text.text).size() <= 240)
          search_draft_ += event.text.text;
        search_composition_.clear();
        continue;
      }
      if (search_open_ && event.type == SDL_TEXTEDITING) {
        search_composition_ = event.edit.text;
        continue;
      }
      if (search_open_ && event.type == SDL_KEYDOWN) {
        if (event.key.keysym.sym == SDLK_RETURN && search_composition_.empty()) Handle(Action::SearchDone);
        else if (event.key.keysym.sym == SDLK_ESCAPE) {
          search_open_ = false; SDL_StopTextInput();
        } else if (event.key.keysym.sym == SDLK_BACKSPACE && search_composition_.empty() && !event.key.repeat) {
          BeginRepeat(Action::ToggleChrome); Handle(Action::ToggleChrome);
        }
        else if (event.key.keysym.sym == SDLK_UP || event.key.keysym.sym == SDLK_DOWN ||
                 event.key.keysym.sym == SDLK_LEFT || event.key.keysym.sym == SDLK_RIGHT) {
          if (search_composition_.empty()) Handle(Translate(event));
        }
        continue;  // text entry must never trigger gameplay shortcuts
      }
      const Action action = Translate(event);
      if (action != Action::None) Handle(action);
      if (!running_) break;
    }
    if (!running_) break;
    PollEvdevInput();
    if (!running_) break;
    PollHeldActions();
    if (!running_) break;
    PollStatus();
    PollHall();
    if (!running_) break;
    if (video_.ConsumeAudioFinished() && !settings_open_ &&
        preferences_.bgm_mode == GbaBgmMode::EightBit) {
      AdvanceEightBitTrack();
      RefreshAudio();
    }
    if (!options_.screenshot_path.empty() && frames >= 2 && !screenshot_action_sent &&
        !options_.screenshot_action.empty()) {
      Action action = Action::None;
      if (options_.screenshot_action == "settings-top") {
        Handle(Action::Menu);
        Handle(Action::Confirm);
      } else if (options_.screenshot_action == "settings-filter") {
        Handle(Action::Menu);
        Handle(Action::Confirm);
        for (int index = 0; index < 6; ++index) Handle(Action::Down);
      } else if (options_.screenshot_action == "settings-bottom") {
        Handle(Action::Menu);
        Handle(Action::Confirm);
        for (int index = 0; index < kSettingsCount - 1; ++index) Handle(Action::Down);
      } else if (options_.screenshot_action == "system-menu") action = Action::Back;
      else if (options_.screenshot_action == "quick-menu") action = Action::QuickMenu;
      else if (options_.screenshot_action == "search") OpenSearch();
      else if (options_.screenshot_action == "help") {
        Handle(Action::Back); Handle(Action::Down); Handle(Action::Confirm);
      } else if (options_.screenshot_action == "right") action = Action::Right;
      else if (options_.screenshot_action == "left") action = Action::Left;
      else if (options_.screenshot_action == "confirm") action = Action::Confirm;
      else if (options_.screenshot_action == "description-down") action = Action::DescriptionDown;
      else if (options_.screenshot_action == "toggle-titles") action = Action::ToggleTitles;
      else if (options_.screenshot_action == "toggle-chrome") action = Action::ToggleChrome;
      else if (options_.screenshot_action == "fullscreen-clean") {
        Handle(Action::ToggleChrome);
        if (preferences_.show_cover_titles) Handle(Action::ToggleTitles);
      } else if (options_.screenshot_action == "status-demo") {
        status_.battery_percent = 68;
        status_.charging = true;
      }
      else if (options_.screenshot_action == "version-menu") {
        active_tab_ = kTabMod;
        selected_ = 0;
        scroll_row_ = 0;
        RefreshVisible();
        const auto found = std::find_if(visible_.begin(), visible_.end(), [&](int index) {
          return !games_[index].alternate_roms.empty();
        });
        if (found != visible_.end()) {
          selected_ = static_cast<int>(found - visible_.begin());
          EnsureSelectionVisible();
          SelectionChanged();
          Handle(Action::Confirm);
        }
      } else if (options_.screenshot_action == "xianjian-video-reuse") {
        active_tab_ = kTabMod;
        selected_ = 0;
        scroll_row_ = 0;
        RefreshVisible();
        const auto select_title = [&](const std::string &title) {
          const auto found = std::find_if(visible_.begin(), visible_.end(), [&](int index) {
            return games_[index].title == title;
          });
          if (found == visible_.end()) return;
          selected_ = static_cast<int>(found - visible_.begin());
          EnsureSelectionVisible();
          SelectionChanged();
        };
        select_title("仙剑奇侠传 1卷");
        select_title("仙剑奇侠传 2卷");
      } else if (options_.screenshot_action == "longest-title") {
        active_tab_ = kTabMod;
        selected_ = 0;
        scroll_row_ = 0;
        RefreshVisible();
        TTF_Font *font = Font(CoverTitleFontSize());
        int widest = -1;
        int widest_width = -1;
        if (font) {
          for (int index = 0; index < static_cast<int>(visible_.size()); ++index) {
            int width = 0;
            if (TTF_SizeUTF8(font, games_[visible_[index]].title.c_str(), &width, nullptr) == 0 &&
                width > widest_width) {
              widest = index;
              widest_width = width;
            }
          }
        }
        if (widest >= 0) {
          selected_ = widest;
          EnsureSelectionVisible();
          SelectionChanged();
        }
      } else if (options_.screenshot_action == "empty-favorite") {
        active_tab_ = kTabFavorite;
        selected_ = 0;
        scroll_row_ = 0;
        RefreshVisible();
        SelectionChanged();
      } else if (options_.screenshot_action == "jump-down-100") {
        Handle(Action::MenuPress);
        Handle(Action::Down);
        Handle(Action::MenuRelease);
      } else if (options_.screenshot_action == "grid-smaller") {
        Handle(Action::MenuPress);
        Handle(Action::Left);
        Handle(Action::MenuRelease);
      } else if (options_.screenshot_action == "grid-larger") {
        Handle(Action::MenuPress);
        Handle(Action::Right);
        Handle(Action::MenuRelease);
      } else if (options_.screenshot_action == "theme-next") {
        Handle(Action::QuickTheme);
      } else if (options_.screenshot_action == "core-og2") {
        active_tab_ = kTabGba;
        selected_ = 0;
        scroll_row_ = 0;
        RefreshVisible();
        const auto found = std::find_if(visible_.begin(), visible_.end(), [&](int index) {
          return games_[index].title == "超级机器人大战OG2";
        });
        if (found != visible_.end()) {
          selected_ = static_cast<int>(found - visible_.begin());
          EnsureSelectionVisible();
          SelectionChanged();
          Handle(Action::CoreMenu);
        }
      } else if (options_.screenshot_action == "launch-og2" ||
                 options_.screenshot_action == "launch-hack" ||
                 options_.screenshot_action == "launch-vib" ||
                 options_.screenshot_action == "launch-version-second") {
        if (options_.screenshot_action == "launch-og2") active_tab_ = kTabGba;
        else if (options_.screenshot_action == "launch-vib") active_tab_ = kTabRumble;
        else active_tab_ = kTabMod;
        selected_ = 0;
        scroll_row_ = 0;
        RefreshVisible();
        const auto found = std::find_if(visible_.begin(), visible_.end(), [&](int index) {
          const GbaGame &game = games_[index];
          if (options_.screenshot_action == "launch-og2") {
            return game.title == "超级机器人大战OG2";
          }
          if (options_.screenshot_action == "launch-version-second") {
            return !game.alternate_roms.empty();
          }
          return game.alternate_roms.empty();
        });
        if (found != visible_.end()) {
          selected_ = static_cast<int>(found - visible_.begin());
          EnsureSelectionVisible();
          SelectionChanged();
          Handle(Action::Confirm);
          if (version_menu_open_) {
            Handle(Action::Down);
            Handle(Action::Confirm);
          }
        }
      }
      else if (options_.screenshot_action == "core-menu") action = Action::CoreMenu;
      else if (options_.screenshot_action == "volume-up") action = Action::VolumeUp;
      else if (options_.screenshot_action == "tab-next") action = Action::TabNext;
      else if (options_.screenshot_action == "tab-previous") action = Action::TabPrevious;
      if (action != Action::None) Handle(action);
      screenshot_action_sent = true;
      screenshot_started_at = SDL_GetTicks();
    }
    UpdateVideoTexture();
    const bool waiting_for_screenshot_action =
        !options_.screenshot_action.empty() && !screenshot_action_sent;
    const bool capture_screenshot_this_frame = !waiting_for_screenshot_action &&
        (options_.screenshot_delay_ms >= 0
            ? SDL_GetTicks() - screenshot_started_at >=
                  static_cast<Uint32>(options_.screenshot_delay_ms)
            : frames >= 2);
    Render();
    if (screenshot_action_sent &&
        (options_.screenshot_action == "tab-next" ||
         options_.screenshot_action == "tab-previous") &&
        tab_transition_started_at_ != 0) {
      screenshot_started_at = tab_transition_started_at_;
    }
    ++frames;
    if (!options_.screenshot_path.empty() && capture_screenshot_this_frame) {
      exit_code_ = SaveScreenshot(options_.screenshot_path) ? 0 : 2;
      running_ = false;
    }
    SDL_RenderPresent(renderer_);
    if (running_ && frame_started - last_interaction_at_ >= kPrewarmIdleDelay) {
      PrewarmNextGameAsset();
    }
    if (frames == 1 && options_.diagnostics) {
      const auto presented = std::chrono::steady_clock::now();
      std::cerr << "[timing] first_present_ms="
                << std::chrono::duration_cast<std::chrono::milliseconds>(
                       presented - startup_started_).count() << '\n';
    }
    const Uint32 frame_elapsed = SDL_GetTicks() - frame_started;
    if (frame_elapsed < 16) SDL_Delay(16 - frame_elapsed);
  }
  if (options_.diagnostics) {
    std::cerr << "[gba] image_loads thumbnail=" << thumbnail_image_loads_
              << " original=" << original_image_loads_
              << " aliases=" << reused_image_aliases_
              << " prewarm_images=" << prewarmed_images_
              << " prewarm_descriptions=" << prewarmed_descriptions_ << '\n';
  }
  if (options_.screenshot_path.empty()) SaveUiState();
  return exit_code_;
}

GbaFrontend::Action GbaFrontend::Translate(const SDL_Event &event) {
  if (event.type == SDL_KEYUP) {
    switch (event.key.keysym.sym) {
      case SDLK_BACKSPACE: EndRepeat(Action::ToggleChrome); break;
      case SDLK_m: return Action::MenuRelease;
      case SDLK_UP: EndRepeat(Action::Up); break;
      case SDLK_DOWN: EndRepeat(Action::Down); break;
      case SDLK_LEFT: EndRepeat(Action::Left); break;
      case SDLK_RIGHT: EndRepeat(Action::Right); break;
      case SDLK_PAGEUP: EndRepeat(Action::DescriptionUp); break;
      case SDLK_PAGEDOWN: EndRepeat(Action::DescriptionDown); break;
      default: break;
    }
    return Action::None;
  }
  if (event.type == SDL_KEYDOWN && event.key.repeat == 0) {
    switch (event.key.keysym.sym) {
      case SDLK_UP: BeginRepeat(Action::Up); return Action::Up;
      case SDLK_DOWN: BeginRepeat(Action::Down); return Action::Down;
      case SDLK_LEFT: BeginRepeat(Action::Left); return Action::Left;
      case SDLK_RIGHT: BeginRepeat(Action::Right); return Action::Right;
      case SDLK_RETURN: case SDLK_SPACE: return Action::Confirm;
      case SDLK_ESCAPE: case SDLK_BACKSPACE: return Action::Back;
      case SDLK_x: return Action::ToggleChrome;
      case SDLK_y: return Action::QuickMenu;
      case SDLK_s: return Action::Favorite;
      case SDLK_q: case SDLK_LEFTBRACKET: return Action::TabPrevious;
      case SDLK_e: case SDLK_RIGHTBRACKET: return Action::TabNext;
      case SDLK_PAGEUP: return Action::QuickTheme;
      case SDLK_PAGEDOWN: return Action::Search;
      case SDLK_c: return Action::CoreMenu;
      case SDLK_m: return Action::MenuPress;
      case SDLK_VOLUMEDOWN: return Action::VolumeDown;
      case SDLK_VOLUMEUP: return Action::VolumeUp;
      case SDLK_POWER: return Action::Power;
      default: break;
    }
  }
  if (has_evdev_gamepad_ && event.type == SDL_JOYBUTTONDOWN &&
      event.jbutton.button == 8) {
    return Action::MenuPress;
  }
  if (has_evdev_gamepad_ && event.type == SDL_JOYBUTTONUP &&
      event.jbutton.button == 8) {
    return Action::MenuRelease;
  }
  if (has_evdev_gamepad_ &&
      (event.type == SDL_CONTROLLERBUTTONDOWN || event.type == SDL_CONTROLLERBUTTONUP ||
       event.type == SDL_JOYBUTTONDOWN || event.type == SDL_JOYBUTTONUP ||
       event.type == SDL_JOYAXISMOTION || event.type == SDL_CONTROLLERAXISMOTION)) {
    return Action::None;
  }
  if (event.type == SDL_CONTROLLERBUTTONDOWN) {
    switch (event.cbutton.button) {
      case SDL_CONTROLLER_BUTTON_A: return Action::Confirm;
      case SDL_CONTROLLER_BUTTON_B: return Action::Back;
      case SDL_CONTROLLER_BUTTON_X: BeginRepeat(Action::ToggleChrome); return Action::ToggleChrome;
      case SDL_CONTROLLER_BUTTON_Y: return Action::QuickMenu;
      case SDL_CONTROLLER_BUTTON_DPAD_UP: BeginRepeat(Action::Up); return Action::Up;
      case SDL_CONTROLLER_BUTTON_DPAD_DOWN: BeginRepeat(Action::Down); return Action::Down;
      case SDL_CONTROLLER_BUTTON_DPAD_LEFT: BeginRepeat(Action::Left); return Action::Left;
      case SDL_CONTROLLER_BUTTON_DPAD_RIGHT: BeginRepeat(Action::Right); return Action::Right;
      case SDL_CONTROLLER_BUTTON_LEFTSHOULDER: return Action::TabPrevious;
      case SDL_CONTROLLER_BUTTON_RIGHTSHOULDER: return Action::TabNext;
      case SDL_CONTROLLER_BUTTON_BACK: return Action::Favorite;
      case SDL_CONTROLLER_BUTTON_START: return search_open_ ? Action::SearchDone : Action::CoreMenu;
      default: break;
    }
  }
  if (event.type == SDL_CONTROLLERBUTTONUP) {
    switch (event.cbutton.button) {
      case SDL_CONTROLLER_BUTTON_X: EndRepeat(Action::ToggleChrome); break;
      case SDL_CONTROLLER_BUTTON_DPAD_UP: EndRepeat(Action::Up); break;
      case SDL_CONTROLLER_BUTTON_DPAD_DOWN: EndRepeat(Action::Down); break;
      case SDL_CONTROLLER_BUTTON_DPAD_LEFT: EndRepeat(Action::Left); break;
      case SDL_CONTROLLER_BUTTON_DPAD_RIGHT: EndRepeat(Action::Right); break;
      case SDL_CONTROLLER_BUTTON_BACK: EndRepeat(Action::DescriptionUp); break;
      case SDL_CONTROLLER_BUTTON_START: EndRepeat(Action::DescriptionDown); break;
      default: break;
    }
  }
  if (event.type == SDL_CONTROLLERAXISMOTION &&
      (event.caxis.axis == SDL_CONTROLLER_AXIS_TRIGGERLEFT ||
       event.caxis.axis == SDL_CONTROLLER_AXIS_TRIGGERRIGHT)) {
    const bool left = event.caxis.axis == SDL_CONTROLLER_AXIS_TRIGGERLEFT;
    bool &held = left ? left_trigger_held_ : right_trigger_held_;
    const bool pressed = event.caxis.value > 16000;
    const bool edge = pressed && !held;
    held = pressed;
    return edge ? (left ? Action::QuickTheme : Action::Search) : Action::None;
  }
  // A mapped controller also emits joystick events: consume only one stream.
  if (controller_ && (event.type == SDL_JOYBUTTONDOWN || event.type == SDL_JOYBUTTONUP ||
                      event.type == SDL_JOYAXISMOTION)) return Action::None;
  if (event.type == SDL_JOYBUTTONDOWN) {
    switch (event.jbutton.button) {
      case 0: return Action::Confirm;
      case 1: return Action::Back;
      case 2: BeginRepeat(Action::ToggleChrome); return Action::ToggleChrome;
      case 3: return Action::QuickMenu;
      case 4: return Action::TabPrevious;
      case 5: return Action::TabNext;
      case 6: return Action::QuickTheme;
      case 7: return Action::Search;
      case 8: return Action::Favorite;
      case 9: return search_open_ ? Action::SearchDone : Action::CoreMenu;
      default: break;
    }
  }
  if (event.type == SDL_JOYBUTTONUP) {
    if (event.jbutton.button == 2) EndRepeat(Action::ToggleChrome);
    else if (event.jbutton.button == 8) EndRepeat(Action::DescriptionUp);
    else if (event.jbutton.button == 9) EndRepeat(Action::DescriptionDown);
  }
  if (event.type == SDL_JOYAXISMOTION || event.type == SDL_CONTROLLERAXISMOTION) {
    const int axis = event.type == SDL_JOYAXISMOTION ? event.jaxis.axis : event.caxis.axis;
    const int value = event.type == SDL_JOYAXISMOTION ? event.jaxis.value : event.caxis.value;
    const int direction = value < -16000 ? -1 : (value > 16000 ? 1 : 0);
    if (axis == 0) {
      if (direction && direction != axis_x_) {
        EndRepeat(axis_x_ < 0 ? Action::Left : Action::Right);
        axis_x_ = direction;
        const Action action = direction < 0 ? Action::Left : Action::Right;
        BeginRepeat(action);
        return action;
      }
      if (!direction) {
        EndRepeat(axis_x_ < 0 ? Action::Left : Action::Right);
        axis_x_ = 0;
      }
    } else if (axis == 1) {
      if (direction && direction != axis_y_) {
        EndRepeat(axis_y_ < 0 ? Action::Up : Action::Down);
        axis_y_ = direction;
        const Action action = direction < 0 ? Action::Up : Action::Down;
        BeginRepeat(action);
        return action;
      }
      if (!direction) {
        EndRepeat(axis_y_ < 0 ? Action::Up : Action::Down);
        axis_y_ = 0;
      }
    }
  }
  return Action::None;
}

void GbaFrontend::BeginRepeat(Action action) {
  const Uint32 now = SDL_GetTicks();
  if (action == Action::ToggleChrome && search_open_ && search_composition_.empty()) {
    if (!backspace_held_) {
      backspace_held_ = true;
      backspace_started_at_ = now;
      next_backspace_at_ = now + 450;
    }
  } else if (action == Action::Up || action == Action::Down ||
      action == Action::Left || action == Action::Right) {
    held_grid_action_ = action;
    next_grid_repeat_at_ = now + kGridRepeatInitialDelay;
  } else if (action == Action::DescriptionUp || action == Action::DescriptionDown) {
    held_description_action_ = action;
    next_description_repeat_at_ = now + kDescriptionRepeatInitialDelay;
  }
}

void GbaFrontend::EndRepeat(Action action) {
  if (action == Action::ToggleChrome) backspace_held_ = false;
  if (held_grid_action_ == action) held_grid_action_ = Action::None;
  if (held_description_action_ == action) held_description_action_ = Action::None;
}

void GbaFrontend::PollHeldActions() {
  const Uint32 now = SDL_GetTicks();
  if (!search_open_) backspace_held_ = false;
  if (backspace_held_ && SDL_TICKS_PASSED(now, next_backspace_at_)) {
    const Uint32 elapsed = now - backspace_started_at_;
    next_backspace_at_ = now + (elapsed >= 1600 ? 45 : elapsed >= 900 ? 90 : 180);
    if (search_composition_.empty()) game_search::Backspace(search_draft_);
    last_interaction_at_ = now;
  }
  if (!menu_button_held_ && !settings_open_ && !core_menu_open_ && !version_menu_open_ &&
      held_grid_action_ != Action::None && SDL_TICKS_PASSED(now, next_grid_repeat_at_)) {
    const Action action = held_grid_action_;
    next_grid_repeat_at_ = now + kGridRepeatInterval;
    Handle(action);
  }
  if (!settings_open_ && !core_menu_open_ && !version_menu_open_ &&
      held_description_action_ != Action::None &&
      SDL_TICKS_PASSED(now, next_description_repeat_at_)) {
    const Action action = held_description_action_;
    next_description_repeat_at_ = now + kDescriptionRepeatInterval;
    Handle(action);
  }
}

void GbaFrontend::OpenEvdevInput() {
#ifndef _WIN32
  for (int index = 0; index < 8; ++index) {
    const std::string path = "/dev/input/event" + std::to_string(index);
    const int fd = open(path.c_str(), O_RDONLY | O_NONBLOCK | O_CLOEXEC);
    if (fd < 0) continue;
    char name[128] = {};
    const bool named = ioctl(fd, EVIOCGNAME(sizeof(name)), name) >= 0;
    const bool input_source = named &&
        (std::string(name) == "dierct-keys-polled" || std::string(name) == "ANBERNIC-keys");
    if (!input_source) {
      close(fd);
      continue;
    }
    if (std::string(name) == "ANBERNIC-keys") has_evdev_gamepad_ = true;
    evdev_input_fds_.push_back(fd);
    if (options_.diagnostics) {
      std::cerr << "[gba] evdev input=" << path << " (" << name << ")\n";
    }
  }
#endif
}

void GbaFrontend::PollEvdevInput() {
#ifndef _WIN32
  for (int fd : evdev_input_fds_) {
    input_event event{};
    while (read(fd, &event, sizeof(event)) == sizeof(event)) {
      Action action = Action::None;
      if (event.type == EV_KEY) {
        if (event.value == 1) {
          switch (event.code) {
            case BTN_SOUTH: action = Action::Confirm; break;
            case BTN_EAST: action = Action::Back; break;
            case BTN_NORTH: BeginRepeat(Action::ToggleChrome); action = Action::ToggleChrome; break;
            case BTN_C: action = Action::QuickMenu; break;
            case BTN_WEST:
              if (menu_button_held_) {
                menu_chord_used_ = true;
                action = Action::QuickTheme;
              } else {
                action = Action::TabPrevious;
              }
              break;
            case BTN_Z:
              if (menu_button_held_) {
                menu_chord_used_ = true;
                action = Action::NextBgm;
              } else {
                action = Action::TabNext;
              }
              break;
            case BTN_TL: action = Action::Favorite; break;
            case BTN_TR: action = search_open_ ? Action::SearchDone : Action::CoreMenu; break;
            case BTN_SELECT: action = Action::QuickTheme; break;
            case BTN_START: action = Action::Search; break;
            case KEY_VOLUMEDOWN: action = Action::VolumeDown; break;
            case KEY_VOLUMEUP: action = Action::VolumeUp; break;
            default: break;
          }
        } else if (event.value == 0) {
          if (event.code == BTN_NORTH) EndRepeat(Action::ToggleChrome);
          else if (event.code == BTN_SELECT) EndRepeat(Action::DescriptionUp);
          else if (event.code == BTN_START) EndRepeat(Action::DescriptionDown);
        }
      } else if (event.type == EV_ABS) {
        if (event.code == ABS_HAT0X) {
          if (event.value < 0) {
            EndRepeat(Action::Right);
            action = Action::Left;
            BeginRepeat(action);
          } else if (event.value > 0) {
            EndRepeat(Action::Left);
            action = Action::Right;
            BeginRepeat(action);
          } else {
            EndRepeat(Action::Left);
            EndRepeat(Action::Right);
          }
        } else if (event.code == ABS_HAT0Y) {
          if (event.value < 0) {
            EndRepeat(Action::Down);
            action = Action::Up;
            BeginRepeat(action);
          } else if (event.value > 0) {
            EndRepeat(Action::Up);
            action = Action::Down;
            BeginRepeat(action);
          } else {
            EndRepeat(Action::Up);
            EndRepeat(Action::Down);
          }
        }
      }
      if (action != Action::None) Handle(action);
    }
  }
#endif
}

void GbaFrontend::Handle(Action action) {
  last_interaction_at_ = SDL_GetTicks();
  if (action == Action::MenuPress) {
    menu_button_held_ = true;
    menu_chord_used_ = false;
    return;
  }
  if (action == Action::MenuRelease) {
    const bool open_menu = menu_button_held_ && !menu_chord_used_;
    menu_button_held_ = false;
    menu_chord_used_ = false;
    if (open_menu) Handle(Action::Menu);
    return;
  }
  if (menu_button_held_ && !sidebar_open_ && !search_open_ && !help_open_ && !settings_open_ && !core_menu_open_ && !version_menu_open_ &&
      (action == Action::Up || action == Action::Down ||
       action == Action::Left || action == Action::Right)) {
    menu_chord_used_ = true;
    if (action == Action::Up || action == Action::Down) {
      action = action == Action::Up ? Action::JumpUpHundred : Action::JumpDownHundred;
    } else {
      EndRepeat(action);
      action = action == Action::Left ? Action::GridSmaller : Action::GridLarger;
    }
  }
  if (exit_dialog_open_ && action != Action::Power && action != Action::VolumeDown &&
      action != Action::VolumeUp) {
    if (action == Action::Back || action == Action::Menu || action == Action::QuickMenu) {
      exit_dialog_open_ = false;
    } else if (action == Action::Up || action == Action::Down) {
      exit_dialog_selected_ = (exit_dialog_selected_ + 4 + (action == Action::Up ? -1 : 1)) % 4;
    } else if (action == Action::Confirm) {
      exit_dialog_open_ = false;
      if (exit_dialog_selected_ != 3) {
        video_.Stop();
        const int codes[] = {0, kExitRestartSystem, kExitPowerOffSystem};
        exit_code_ = codes[exit_dialog_selected_];
        running_ = false;
      }
    }
    return;
  }
  if (search_open_ && action != Action::Power && action != Action::VolumeDown &&
      action != Action::VolumeUp) { HandleSearch(action); return; }
  if (help_open_ && action != Action::Power && action != Action::VolumeDown &&
      action != Action::VolumeUp) {
    if (action == Action::Back || action == Action::Confirm || action == Action::Menu)
      help_open_ = false;
    return;
  }
  if (sidebar_open_ && action != Action::Power && action != Action::VolumeDown &&
      action != Action::VolumeUp) {
    if (action == Action::Back || action == Action::Menu || action == Action::QuickMenu) {
      sidebar_open_ = false;
    } else if (action == Action::Search) OpenSearch();
    else if (action == Action::Up || action == Action::Down) {
      const int count = quick_menu_open_ ? kQuickSettingsCount : 3;
      sidebar_selected_ = (sidebar_selected_ + count + (action == Action::Up ? -1 : 1)) % count;
    } else if (quick_menu_open_) {
      if (action == Action::QuickTheme) AdjustSetting(11, Action::Right);
      else if (sidebar_selected_ == 0 && action == Action::Confirm) OpenSearch();
      else AdjustSetting(kQuickSettings[sidebar_selected_], action);
    } else if (action == Action::Confirm) {
      if (sidebar_selected_ == 0) {
        sidebar_open_ = false;
        settings_open_ = true;
        settings_selected_ = settings_scroll_ = 0;
        RefreshAudio();
      } else if (sidebar_selected_ == 1) help_open_ = true;
      else {
        exit_dialog_open_ = true;
        exit_dialog_selected_ = 3;
        held_grid_action_ = Action::None;
      }
    }
    return;
  }
  if (action == Action::GridSmaller || action == Action::GridLarger) {
    const int direction = action == Action::GridSmaller ? 1 : -1;
    const int value = (static_cast<int>(preferences_.grid_size) + 3 + direction) % 3;
    preferences_.grid_size = static_cast<GbaGridSize>(value);
    SavePreferences();
    EnsureSelectionVisible();
    return;
  }
  if (action == Action::QuickTheme) {
    const int value =
        (static_cast<int>(preferences_.theme_color) + 1) % kGbaThemeColorCount;
    preferences_.theme_color = static_cast<GbaThemeColor>(value);
    SavePreferences();
    return;
  }
  if (action == Action::NextBgm) {
    if (preferences_.bgm_mode == GbaBgmMode::EightBit) {
      AdvanceEightBitTrack();
      RefreshAudio();
    }
    return;
  }
  if (action == Action::Power) {
    video_.Stop();
    exit_code_ = kExitSuspendManual;
    running_ = false;
    return;
  }
  if (action == Action::VolumeDown || action == Action::VolumeUp) {
    const Uint32 now = SDL_GetTicks();
    if (action == last_volume_action_ && now - last_volume_action_at_ < 80) return;
    last_volume_action_ = action;
    last_volume_action_at_ = now;
    status_.volume = services_.ChangeVolume(action == Action::VolumeUp ? 1 : -1);
    volume_hint_until_ = now + 1300;
    return;
  }
  if (version_menu_open_) {
    description_highlighted_ = false;
    const std::vector<std::string> roms = SelectedRomOptions();
    if (roms.empty()) {
      version_menu_open_ = false;
    } else if (action == Action::Up) {
      version_menu_selected_ =
          (version_menu_selected_ + static_cast<int>(roms.size()) - 1) % roms.size();
      EnsureVersionMenuVisible();
    } else if (action == Action::Down) {
      version_menu_selected_ = (version_menu_selected_ + 1) % roms.size();
      EnsureVersionMenuVisible();
    } else if (action == Action::Confirm) {
      if (GbaGame *game = SelectedGame()) {
        const int chosen = std::clamp(version_menu_selected_, 0,
                                      static_cast<int>(roms.size()) - 1);
        if (LaunchGame(*game, roms[chosen])) return;
      }
      version_menu_open_ = false;
    } else if (action == Action::Back) {
      version_menu_open_ = false;
    }
    return;
  }
  if (core_menu_open_) {
    description_highlighted_ = false;
    if (action == Action::Up) {
      core_menu_selected_ = (core_menu_selected_ + 3) % 4;
    } else if (action == Action::Down) {
      core_menu_selected_ = (core_menu_selected_ + 1) % 4;
    } else if (action == Action::Confirm) {
      if (GbaGame *game = SelectedGame()) {
        game->core = static_cast<GbaCore>(std::clamp(core_menu_selected_, 0, 3));
        game->core_overridden = game->core != game->default_core;
        state_.Save(games_);
      }
      core_menu_open_ = false;
    } else if (action == Action::Back) {
      core_menu_open_ = false;
    }
    return;
  }
  if (settings_open_) {
    description_highlighted_ = false;
    if (action == Action::Back || action == Action::Menu) {
      settings_open_ = false;
      RefreshAudio();
    } else if (action == Action::Up) {
      settings_selected_ = (settings_selected_ + kSettingsCount - 1) % kSettingsCount;
      EnsureSettingsVisible();
    } else if (action == Action::Down) {
      settings_selected_ = (settings_selected_ + 1) % kSettingsCount;
      EnsureSettingsVisible();
    } else {
      AdjustSetting(settings_selected_, action);
    }
    return;
  }
  if (action == Action::Search) { OpenSearch(); return; }
  if (action == Action::Menu || action == Action::Back || action == Action::QuickMenu) {
    sidebar_open_ = true;
    quick_menu_open_ = action == Action::QuickMenu;
    sidebar_selected_ = 0;
    held_grid_action_ = Action::None;
    return;
  }
  if (action == Action::CoreMenu) {
    description_highlighted_ = false;
    if (const GbaGame *game = SelectedGame()) {
      core_menu_selected_ = static_cast<int>(game->core);
      core_menu_open_ = true;
    }
    return;
  }
  if (action == Action::ToggleTitles) {
    preferences_.show_cover_titles = !preferences_.show_cover_titles;
    SavePreferences();
    return;
  }
  if (action == Action::ToggleChrome) {
    const Uint32 now = SDL_GetTicks();
    chrome_animation_from_ = ChromeHiddenProgress(now);
    preferences_.fullscreen_grid = !preferences_.fullscreen_grid;
    chrome_animation_to_ = preferences_.fullscreen_grid ? 1.0f : 0.0f;
    chrome_animation_started_at_ = now;
    description_highlighted_ = false;
    SavePreferences();
    EnsureSelectionVisible();
    video_.StopVideo();
    video_start_at_ = 0;
    video_version_ = 0;
    if (video_texture_) { SDL_DestroyTexture(video_texture_); video_texture_ = nullptr; }
    selected_video_path_.clear();
    SelectionChanged();
    return;
  }
  if (action == Action::TabPrevious || action == Action::TabNext) {
    const Uint32 now = SDL_GetTicks();
    if (tab_transition_direction_ != 0 &&
        (tab_transition_pending_start_ ||
         now - tab_transition_started_at_ < kTabTransitionDuration)) {
      return;
    }
    tab_transition_from_ = active_tab_;
    tab_transition_direction_ = action == Action::TabNext ? 1 : -1;
    active_tab_ = WrappedTab(active_tab_ + tab_transition_direction_);
    selected_ = 0;
    scroll_row_ = 0;
    description_highlighted_ = false;
    description_scroll_line_ = 0;
    cover_scale_animations_.clear();
    RefreshVisible();
    const int preload_count = std::min(
        static_cast<int>(visible_.size()),
        (GridVisibleRows() + 1) * GridColumns());
    for (int index = 0; index < preload_count; ++index) {
      Image(games_[visible_[index]].cover_path);
    }
    if (const GbaGame *game = SelectedGame()) {
      Image(game->logo_path);
      Image(game->cover_path);
    }
    const Uint32 animation_now = SDL_GetTicks();
    tab_transition_started_at_ = 0;
    tab_transition_pending_start_ = true;
    if (!visible_.empty()) {
      StartCoverScaleAnimation(visible_[selected_], 1.0f, 1.15f, animation_now);
    }
    SelectionChanged();
    return;
  }

  const int columns = GridColumns();
  const int old = selected_;
  if (action == Action::JumpUpHundred && !visible_.empty()) {
    selected_ = std::max(0, selected_ - 100);
  } else if (action == Action::JumpDownHundred && !visible_.empty()) {
    selected_ = std::min(static_cast<int>(visible_.size()) - 1, selected_ + 100);
  } else if (action == Action::Left && selected_ % columns > 0) {
    --selected_;
  } else if (action == Action::Right && selected_ + 1 < static_cast<int>(visible_.size()) &&
             selected_ % columns < columns - 1) {
    ++selected_;
  } else if (action == Action::Up && selected_ >= columns) {
    selected_ -= columns;
  } else if (action == Action::Down && selected_ + columns < static_cast<int>(visible_.size())) {
    selected_ += columns;
  }
  if (action == Action::Favorite) {
    if (GbaGame *game = SelectedGame()) {
      const std::string id = game->id;
      game->favorite = !game->favorite;
      game->favorite_order = 0;
      if (game->favorite) {
        for (const GbaGame &entry : games_)
          game->favorite_order = std::max(game->favorite_order, entry.favorite_order);
        ++game->favorite_order;
      }
      state_.Save(games_);
      SetOsd(game->favorite ? "已加入收藏" : "已取消收藏");
      if (!game->favorite && active_tab_ == kTabFavorite)
        active_tab_ = game->is_rumble ? kTabRumble : game->is_mod ? kTabMod : kTabGba;
      RefreshVisible();
      const auto found = std::find_if(visible_.begin(), visible_.end(), [&](int index) {
        return games_[index].id == id;
      });
      if (found != visible_.end()) selected_ = static_cast<int>(found - visible_.begin());
      EnsureSelectionVisible();
      SelectionChanged();
      return;
    }
  } else if (action == Action::Confirm) {
    if (GbaGame *game = SelectedGame()) {
      if (!game->alternate_roms.empty()) {
        version_menu_open_ = true;
        version_menu_selected_ = 0;
        version_menu_scroll_ = 0;
      } else {
        LaunchGame(*game, game->rom_path);
      }
    }
  }
  if (old != selected_) {
    const Uint32 now = SDL_GetTicks();
    if (old >= 0 && old < static_cast<int>(visible_.size())) {
      StartCoverScaleAnimation(visible_[old], 1.15f, 1.0f, now);
    }
    if (selected_ >= 0 && selected_ < static_cast<int>(visible_.size())) {
      StartCoverScaleAnimation(visible_[selected_], 1.0f, 1.15f, now);
    }
    SelectionChanged();
  }
  EnsureSelectionVisible();
}

void GbaFrontend::StartCoverScaleAnimation(int game_index, float fallback_from,
                                           float target, Uint32 now) {
  const float current = CoverScale(game_index, fallback_from, now);
  cover_scale_animations_[game_index] = {current, target, now};
}

float GbaFrontend::CoverScale(int game_index, float fallback, Uint32 now) {
  const auto found = cover_scale_animations_.find(game_index);
  if (found == cover_scale_animations_.end()) return fallback;

  const float progress = AnimationProgress(found->second.started_at,
                                           kCoverScaleDuration, now);
  const float scale = found->second.from +
      (found->second.to - found->second.from) * InOutQuad(progress);
  if (progress >= 1.0f) cover_scale_animations_.erase(found);
  return scale;
}

void GbaFrontend::RefreshVisible() {
  visible_.clear();
  for (int i = 0; i < static_cast<int>(games_.size()); ++i) {
    const GbaGame &game = games_[i];
    if (!game_search::Matches(search_keys_[i], search_query_)) continue;
    if (active_tab_ == kTabRecent && game.recent_order == 0) continue;
    if (active_tab_ == kTabGba && (game.is_mod || game.is_rumble)) continue;
    if (active_tab_ == kTabMod && (!game.is_mod || game.is_rumble)) continue;
    if (active_tab_ == kTabRumble && !game.is_rumble) continue;
    if (active_tab_ == kTabFavorite && !game.favorite) continue;
    visible_.push_back(i);
  }
  if (active_tab_ == kTabRecent) {
    std::sort(visible_.begin(), visible_.end(), [&](int left, int right) {
      return games_[left].recent_order > games_[right].recent_order;
    });
  }
  std::stable_sort(visible_.begin(), visible_.end(), [&](int left, int right) {
    if (games_[left].favorite != games_[right].favorite) return games_[left].favorite;
    return games_[left].favorite && games_[left].favorite_order < games_[right].favorite_order;
  });
  selected_ = std::clamp(selected_, 0, std::max(0, static_cast<int>(visible_.size()) - 1));
  EnsureSelectionVisible();
}

void GbaFrontend::PrewarmNextGameAsset() {
  if (!renderer_ || settings_open_ || core_menu_open_ || version_menu_open_ ||
      visible_.empty() ||
      tab_transition_direction_ != 0 || tab_transition_pending_start_) {
    return;
  }

  std::vector<int> nearby;
  nearby.reserve(24);
  for (int distance = 1; distance <= 12; ++distance) {
    const int next = selected_ + distance;
    const int previous = selected_ - distance;
    if (next < static_cast<int>(visible_.size())) nearby.push_back(next);
    if (previous >= 0) nearby.push_back(previous);
  }

  // Only compact cover thumbnails are safe to decode between navigation
  // frames. Logos and descriptions are handled on demand using their own
  // optimized assets and linear wrapping path.
  for (int visible_index : nearby) {
    const GbaGame &game = games_[visible_[visible_index]];
    if (!game.cover_path.empty() && images_.find(game.cover_path) == images_.end()) {
      if (Image(game.cover_path)) ++prewarmed_images_;
      return;
    }
  }
}

int GbaFrontend::GridColumns() const {
  const int fullscreen_extra = (preferences_.fullscreen_grid ? 1 : 0) + (canvas_width_ - 720) / 160;
  switch (preferences_.grid_size) {
    case GbaGridSize::Large: return 3 + fullscreen_extra;
    case GbaGridSize::Small: return 5 + fullscreen_extra;
    case GbaGridSize::Medium: return 4 + fullscreen_extra;
  }
  return 4 + fullscreen_extra;
}

int GbaFrontend::GridCardSize() const {
  const int width = preferences_.fullscreen_grid ? canvas_width_ : canvas_width_ - kGridX;
  const int inset = preferences_.fullscreen_grid ? kFullscreenGridInset : kGridInset;
  return (width - inset * 2) / GridColumns();
}

int GbaFrontend::GridVisibleRows() const {
  const int height = preferences_.fullscreen_grid ? canvas_height_ : canvas_height_ - kGridY;
  const int inset = preferences_.fullscreen_grid ? kFullscreenGridInset : kGridInset;
  return std::max(1, (height - inset * 2) / GridCardSize());
}

float GbaFrontend::ChromeHiddenProgress(Uint32 now) const {
  if (chrome_animation_from_ == chrome_animation_to_) return chrome_animation_to_;
  const float elapsed = AnimationProgress(chrome_animation_started_at_,
                                          kChromeTransitionDuration, now);
  return chrome_animation_from_ +
      (chrome_animation_to_ - chrome_animation_from_) * OutCubic(elapsed);
}

int GbaFrontend::CoverTitleFontSize() const {
  return kCoverTitleBaseFontSize +
      std::clamp(preferences_.cover_title_size_level, 0, kFontSizeLevelCount - 1);
}

int GbaFrontend::DescriptionFontSize() const {
  return kDescriptionBaseFontSize +
      std::clamp(preferences_.description_size_level, 0, kFontSizeLevelCount - 1);
}

int GbaFrontend::DescriptionLineHeight() const {
  return DescriptionFontSize() + 5;
}

int GbaFrontend::DescriptionVisibleLines() const {
  return std::max(1, (kDescriptionTextHeight + canvas_height_ - 480) / DescriptionLineHeight());
}

void GbaFrontend::EnsureSelectionVisible() {
  const int row = selected_ / GridColumns();
  if (row < scroll_row_) scroll_row_ = row;
  if (row >= scroll_row_ + GridVisibleRows()) {
    scroll_row_ = row - GridVisibleRows() + 1;
  }
  scroll_row_ = std::max(0, scroll_row_);
}

void GbaFrontend::EnsureSettingsVisible() {
  constexpr int kVisibleRows = 6;
  if (settings_selected_ < settings_scroll_) settings_scroll_ = settings_selected_;
  if (settings_selected_ >= settings_scroll_ + kVisibleRows) {
    settings_scroll_ = settings_selected_ - kVisibleRows + 1;
  }
  settings_scroll_ = std::clamp(settings_scroll_, 0,
                                std::max(0, kSettingsCount - kVisibleRows));
}

void GbaFrontend::EnsureVersionMenuVisible() {
  if (version_menu_selected_ < version_menu_scroll_) {
    version_menu_scroll_ = version_menu_selected_;
  }
  if (version_menu_selected_ >= version_menu_scroll_ + kVersionMenuVisibleRows) {
    version_menu_scroll_ = version_menu_selected_ - kVersionMenuVisibleRows + 1;
  }
  const int count = static_cast<int>(SelectedRomOptions().size());
  version_menu_scroll_ = std::clamp(
      version_menu_scroll_, 0, std::max(0, count - kVersionMenuVisibleRows));
}

void GbaFrontend::SavePreferences() {
  if (!preferences_store_.Save(preferences_) && options_.diagnostics) {
    std::cerr << "[gba] failed to save preferences\n";
  }
}

void GbaFrontend::SelectEightBitTrack() {
  eight_bit_tracks_.clear();
  selected_eight_bit_index_ = 0;
  auto collect_tracks = [](const fs::path &directory) {
    std::vector<fs::path> tracks;
    std::error_code error;
    for (fs::directory_iterator iterator(directory, error), end;
         !error && iterator != end; iterator.increment(error)) {
      if (!iterator->is_regular_file(error)) continue;
      std::string extension = iterator->path().extension().u8string();
      std::transform(extension.begin(), extension.end(), extension.begin(),
                     [](unsigned char value) { return static_cast<char>(std::tolower(value)); });
      if (extension == ".mp3") tracks.push_back(iterator->path());
    }
    return tracks;
  };

  std::vector<fs::path> tracks = collect_tracks(
      fs::u8path(options_.app_dir) / "assets/music");
  if (tracks.size() <= 1) {
    tracks = collect_tracks(fs::u8path(options_.state_dir) / "music");
  }
  if (tracks.size() <= 1) {
    tracks = collect_tracks(fs::u8path(options_.app_dir) / "assets/music/builtin");
  }
  if (tracks.size() <= 1) {
    tracks = collect_tracks(fs::u8path(options_.app_dir) / "../assets/music/builtin");
  }
  if (tracks.empty()) {
    selected_eight_bit_track_.clear();
    if (options_.diagnostics) std::cerr << "[gba] no 8bit music tracks found\n";
    return;
  }
  std::sort(tracks.begin(), tracks.end(), [](const fs::path &left, const fs::path &right) {
    return left.filename().u8string() < right.filename().u8string();
  });
  eight_bit_tracks_.reserve(tracks.size());
  for (const fs::path &track : tracks) eight_bit_tracks_.push_back(track.u8string());

  std::string previous_name;
  const fs::path history_path = fs::u8path(options_.state_dir) / "last_8bit_track.txt";
  std::ifstream(history_path) >> previous_name;
  size_t selected = tracks.size();
  if (options_.reuse_bgm_track && !previous_name.empty()) {
    const auto previous = std::find_if(tracks.begin(), tracks.end(), [&](const fs::path &track) {
      return track.filename().u8string() == previous_name;
    });
    if (previous != tracks.end()) selected = static_cast<size_t>(previous - tracks.begin());
  }
  if (selected >= tracks.size()) {
    const auto system_seed = std::chrono::high_resolution_clock::now().time_since_epoch().count();
    const auto steady_seed = std::chrono::steady_clock::now().time_since_epoch().count();
    std::mt19937_64 generator(static_cast<std::uint64_t>(system_seed) ^
                              (static_cast<std::uint64_t>(steady_seed) << 1));
    selected = std::uniform_int_distribution<size_t>(0, tracks.size() - 1)(generator);
    if (tracks.size() > 1 && tracks[selected].filename().u8string() == previous_name) {
      selected = (selected + 1) % tracks.size();
    }
  }
  selected_eight_bit_index_ = selected;
  selected_eight_bit_track_ = eight_bit_tracks_[selected_eight_bit_index_];

  std::error_code error;
  fs::create_directories(history_path.parent_path(), error);
  std::ofstream(history_path, std::ios::trunc) << tracks[selected].filename().u8string() << '\n';
  if (options_.diagnostics) {
    std::cerr << "[gba] 8bit track=" << tracks[selected].filename().u8string()
              << " pool=" << tracks.size() << '\n';
  }
}

void GbaFrontend::AdvanceEightBitTrack() {
  if (eight_bit_tracks_.empty()) SelectEightBitTrack();
  if (eight_bit_tracks_.empty()) return;
  selected_eight_bit_index_ = (selected_eight_bit_index_ + 1) % eight_bit_tracks_.size();
  selected_eight_bit_track_ = eight_bit_tracks_[selected_eight_bit_index_];

  const fs::path selected_path = fs::u8path(selected_eight_bit_track_);
  const fs::path history_path = fs::u8path(options_.state_dir) / "last_8bit_track.txt";
  std::error_code error;
  fs::create_directories(history_path.parent_path(), error);
  std::ofstream(history_path, std::ios::trunc) << selected_path.filename().u8string() << '\n';
  if (options_.diagnostics) {
    std::cerr << "[gba] 8bit next=" << selected_path.filename().u8string()
              << " index=" << selected_eight_bit_index_ + 1
              << '/' << eight_bit_tracks_.size() << '\n';
  }
}

void GbaFrontend::RefreshAudio() {
  if (settings_open_) {
    video_.SetAudio(std::string{}, false);
    return;
  }
  if (options_.screenshot_path.empty()) {
    if (preferences_.bgm_mode == GbaBgmMode::EightBit) {
      if (selected_eight_bit_track_.empty()) SelectEightBitTrack();
      video_.SetAudio(selected_eight_bit_track_, false);
      return;
    }
    if (preferences_.bgm_mode == GbaBgmMode::GameAudio && !preferences_.fullscreen_grid && !options_.no_video &&
        video_start_at_ == 0 && !selected_video_path_.empty()) {
      video_.SetAudio(selected_video_path_, preferences_.preview_video_loop);
      return;
    }
  }
  video_.SetAudio(std::string{}, false);
}

void GbaFrontend::SelectionChanged() {
  description_scroll_line_ = 0;
  description_highlighted_ = false;
  selected_title_started_at_ = SDL_GetTicks();
  const GbaGame *game = SelectedGame();
  description_next_at_ = SDL_GetTicks() + 3500;
  const std::string path = game && !preferences_.fullscreen_grid ? game->video_path : std::string{};
  if (path == selected_video_path_) {
    RefreshAudio();
    return;
  }
  if (SameSampledFile(path, selected_video_path_)) {
    if (options_.diagnostics) {
      std::cerr << "[gba] reuse identical preview video=" << path << '\n';
    }
    RefreshAudio();
    return;
  }
  selected_video_path_ = path;
  if (options_.diagnostics) std::cerr << "[gba] preview video=" << path << '\n';
  video_.StopVideo();
  video_start_at_ = 0;
  video_version_ = 0;
  if (video_texture_) { SDL_DestroyTexture(video_texture_); video_texture_ = nullptr; }
  if (!options_.no_video && options_.screenshot_path.empty() && !path.empty()) {
    video_start_at_ = SDL_GetTicks() + 300;
  }
  RefreshAudio();
}

void GbaFrontend::PollStatus() {
  const Uint32 now = SDL_GetTicks();
  if (now < next_status_poll_) return;
  status_ = services_.ReadStatus();
  next_status_poll_ = now + 2000;
}

void GbaFrontend::PollHall() {
  const Uint32 now = SDL_GetTicks();
  if (now < next_hall_poll_) return;
  next_hall_poll_ = now + 250;
  HandleHallState(services_.HallState());
}

void GbaFrontend::HandleHallState(int current) {
  if (hall_state_ == 1 && current == 0) {
    // Let the launcher suspend after SDL exits and recreate it on wake.
    video_.Stop();
    exit_code_ = preferences_.super_standby ? kExitSuspendAutomaticDeep : kExitSuspendAutomatic;
    running_ = false;
  }
  if (current >= 0) hall_state_ = current;
}

void GbaFrontend::RestoreUiState() {
  GbaUiState saved;
  if (!ui_state_.Load(&saved)) return;
  active_tab_ = std::clamp(saved.active_tab, 0, kTabCount - 1);
  scroll_row_ = std::max(0, saved.scroll_row);
  restored_scroll_row_ = scroll_row_;
  settings_open_ = false;
  core_menu_open_ = false;
  version_menu_open_ = false;
  settings_selected_ = std::clamp(saved.settings_selected, 0, kSettingsCount - 1);
  EnsureSettingsVisible();
  restored_game_id_ = std::move(saved.selected_game_id);
}

void GbaFrontend::SaveUiState() const {
  GbaUiState saved;
  saved.active_tab = active_tab_;
  saved.scroll_row = scroll_row_;
  saved.settings_open = settings_open_;
  saved.settings_selected = settings_selected_;
  if (const GbaGame *game = SelectedGame()) saved.selected_game_id = game->id;
  if (!ui_state_.Save(saved) && options_.diagnostics) {
    std::cerr << "[gba] failed to save UI state\n";
  }
}

void GbaFrontend::UpdateVideoTexture() {
  if (preferences_.fullscreen_grid) return;
  if (!settings_open_ && video_start_at_ != 0 &&
      SDL_TICKS_PASSED(SDL_GetTicks(), video_start_at_)) {
    video_start_at_ = 0;
    video_.StartVideo(selected_video_path_, 300, 169,
                      preferences_.preview_video_loop);
    RefreshAudio();
  }
  std::vector<std::uint8_t> pixels;
  if (!video_.CopyLatestFrame(&pixels, &video_version_)) return;
  if (!video_texture_) {
    video_texture_ = SDL_CreateTexture(renderer_, SDL_PIXELFORMAT_RGBA32,
                                       SDL_TEXTUREACCESS_STREAMING, 300, 169);
  }
  if (video_texture_) SDL_UpdateTexture(video_texture_, nullptr, pixels.data(), 300 * 4);
}

void GbaFrontend::Render() {
  Fill(renderer_, SDL_Rect{0, 0, canvas_width_, canvas_height_}, kBackground);
  if (settings_open_) {
    RenderSettings();
    return;
  }
  const float chrome_hidden = ChromeHiddenProgress(SDL_GetTicks());
  RenderTopBar(-static_cast<int>(std::lround(45.0f * chrome_hidden)));
  RenderGameInfo(-static_cast<int>(std::lround(240.0f * chrome_hidden)));
  RenderGrid(chrome_hidden);
  if (tab_transition_pending_start_) {
    tab_transition_started_at_ = SDL_GetTicks();
    tab_transition_pending_start_ = false;
  }
  RenderHints();
  if (sidebar_open_ || help_open_) RenderSidebar();
  if (search_open_) RenderSearch();
  if (core_menu_open_) RenderCoreMenu();
  if (version_menu_open_) RenderVersionMenu();
  RenderOsd();
  if (exit_dialog_open_) RenderExitDialog();
}

void GbaFrontend::RenderTopBar(int y_offset) {
  SDL_Rect viewport{0, y_offset, canvas_width_, canvas_height_};
  SDL_RenderSetViewport(renderer_, &viewport);
  const ThemeColors theme = ColorsForTheme(preferences_.theme_color);
  Fill(renderer_, SDL_Rect{0, 0, canvas_width_, 45}, theme.bar);

  struct TabVisual {
    int tab = 0;
    float x = 0.0f;
    float opacity = 1.0f;
    bool selected = false;
  };
  std::vector<TabVisual> visuals;
  const Uint32 now = SDL_GetTicks();
  float progress = 1.0f;
  if (tab_transition_direction_ != 0 && !tab_transition_pending_start_) {
    progress = AnimationProgress(tab_transition_started_at_, kTabTransitionDuration, now);
    if (progress >= 1.0f) tab_transition_direction_ = 0;
  } else if (tab_transition_pending_start_) {
    progress = 0.0f;
  }
  if (tab_transition_direction_ == 0) {
    float x = 0.0f;
    for (int slot = 0; slot < kTabCount; ++slot) {
      const int tab = WrappedTab(active_tab_ + slot);
      visuals.push_back({tab, x, 1.0f, slot == 0});
      x += static_cast<float>(kTabWidths[tab]);
    }
  } else {
    const float eased = InOutQuad(progress);
    std::vector<int> old_order;
    std::vector<float> old_positions;
    float x = 0.0f;
    for (int slot = 0; slot < kTabCount; ++slot) {
      const int tab = WrappedTab(tab_transition_from_ + slot);
      old_order.push_back(tab);
      old_positions.push_back(x);
      x += static_cast<float>(kTabWidths[tab]);
    }
    const float end_x = x;

    if (tab_transition_direction_ > 0) {
      const float distance = static_cast<float>(kTabWidths[old_order.front()]);
      visuals.push_back({old_order.front(), old_positions.front() - eased * distance,
                         1.0f - eased, false});
      for (int slot = 1; slot < kTabCount; ++slot) {
        visuals.push_back({old_order[slot], old_positions[slot] - eased * distance,
                           1.0f, old_order[slot] == active_tab_});
      }
      visuals.push_back({old_order.front(), end_x - eased * distance, eased, false});
    } else {
      const int incoming = active_tab_;
      const float distance = static_cast<float>(kTabWidths[incoming]);
      visuals.push_back({incoming, -distance + eased * distance,
                         eased, true});
      for (int slot = 0; slot < kTabCount - 1; ++slot) {
        visuals.push_back({old_order[slot], old_positions[slot] + eased * distance,
                           1.0f, false});
      }
      visuals.push_back({incoming, old_positions.back() + eased * distance,
                         1.0f - eased, false});
    }
  }

  const SDL_Rect tabs_clip{0, 0, std::min(574, canvas_width_ - 170), 45};
  SDL_RenderSetClipRect(renderer_, &tabs_clip);
  for (const TabVisual &visual : visuals) {
    if (visual.selected) {
      FillRightSlantTab(renderer_, static_cast<int>(std::lround(visual.x)), 1,
                        kTabWidths[visual.tab], 42, 8,
                        WithOpacity(theme.selected, visual.opacity));
    }
  }
  for (const TabVisual &visual : visuals) {
    const int tab_x = static_cast<int>(std::lround(visual.x));
    const int width = kTabWidths[visual.tab];
    DrawText(kTabs[visual.tab], tab_x + width / 2, 12, 16,
             WithOpacity(visual.selected ? kInk : SDL_Color{160,164,170,255},
                         visual.opacity),
             width - 18, true);
    const SDL_Color divider = WithOpacity(SDL_Color{128,132,138,255}, visual.opacity);
    SDL_SetRenderDrawColor(renderer_, divider.r, divider.g, divider.b, divider.a);
    SDL_RenderDrawLine(renderer_, tab_x + width - 8, 42, tab_x + width + 8, 2);
  }
  SDL_RenderSetClipRect(renderer_, nullptr);
  SDL_SetRenderDrawColor(renderer_, 215, 217, 220, 255);
  SDL_RenderDrawLine(renderer_, 0, 44, canvas_width_ - 1, 44);
  viewport.x = canvas_width_ - 720;
  viewport.w = std::max(720, canvas_width_);
  SDL_RenderSetViewport(renderer_, &viewport);
  if (status_.volume >= 0 && now < volume_hint_until_) {
    DrawTextRight("前端音量 " + std::to_string(status_.volume), 574, 14, 13,
                  SDL_Color{255, 255, 255, 255});
  } else {
    const GbaGame *game = SelectedGame();
    const std::string current = game && !game->sort_key.empty()
        ? game->sort_key : PaddedNumber(visible_.empty() ? 0 : selected_ + 1);
    DrawTextRight(current, 574, 3, 12, SDL_Color{255, 255, 255, 255});
    DrawTextRight(PaddedNumber(static_cast<int>(visible_.size())), 574, 23, 12,
                  SDL_Color{255, 255, 255, 255});
  }

  constexpr SDL_Color kStatusWhite{255, 255, 255, 255};
  const SDL_Rect logo_bounds{584, 2, 36, 36};
  if (SDL_Texture *logo = Image(
          (fs::u8path(options_.app_dir) / "assets/ui/pegasus_g.png").u8string())) {
    DrawTextureFit(logo, logo_bounds, false);
  }

  DrawTextRight("天马G ROC移植", 716, 3, 12, kStatusWhite);
  DrawText(ClockText(), 630, 23, 12, kStatusWhite, 36);

  const SDL_Rect battery_body{695, 25, 18, 10};
  Stroke(renderer_, battery_body, kStatusWhite);
  Fill(renderer_, SDL_Rect{714, 28, 2, 4}, kStatusWhite);
  if (status_.battery_percent >= 0) {
    const int percent = std::clamp(status_.battery_percent, 0, 100);
    const int fill_width = (battery_body.w - 4) * percent / 100;
    SDL_Color cell_color = kStatusWhite;
    if (percent >= 50) cell_color = SDL_Color{76, 219, 111, 255};
    else if (percent <= 10) cell_color = SDL_Color{235, 71, 71, 255};
    else if (percent <= 20) cell_color = SDL_Color{244, 201, 72, 255};
    if (fill_width > 0) {
      Fill(renderer_, SDL_Rect{battery_body.x + 2, battery_body.y + 2,
                               fill_width, battery_body.h - 4},
           cell_color);
    }
    if (status_.charging) {
      constexpr SDL_Color kBolt{255, 255, 255, 255};
      const SDL_Vertex bolt[] = {
          {{704.0f, 26.0f}, kBolt, {0.0f, 0.0f}},
          {{700.0f, 31.0f}, kBolt, {0.0f, 0.0f}},
          {{703.0f, 31.0f}, kBolt, {0.0f, 0.0f}},
          {{702.0f, 34.0f}, kBolt, {0.0f, 0.0f}},
          {{708.0f, 29.0f}, kBolt, {0.0f, 0.0f}},
          {{705.0f, 29.0f}, kBolt, {0.0f, 0.0f}},
      };
      constexpr int bolt_indices[] = {0, 1, 2, 0, 2, 5, 5, 2, 3, 5, 3, 4};
      SDL_RenderGeometry(renderer_, nullptr, bolt, 6, bolt_indices, 12);
    }
  }
  const std::string battery_text = status_.battery_percent >= 0
      ? std::to_string(status_.battery_percent) + "%" : "--%";
  DrawTextRight(battery_text, 690, 23, 12, kStatusWhite);
  SDL_RenderSetViewport(renderer_, nullptr);
}

void GbaFrontend::RenderGameInfo(int x_offset) {
  SDL_Rect viewport{x_offset, 0, canvas_width_, canvas_height_};
  SDL_RenderSetViewport(renderer_, &viewport);
  const GbaGame *game = SelectedGame();
  Fill(renderer_, SDL_Rect{0, 45, 240, canvas_height_ - 45}, kBackground);
  SDL_SetRenderDrawColor(renderer_, 50, 52, 55, 255);
  SDL_RenderDrawLine(renderer_, 239, 45, 239, canvas_height_ - 1);

  if (!game) {
    DrawText("此分类暂无游戏", 120, 214, 18, kMuted, 208, true);
    SDL_RenderSetViewport(renderer_, nullptr);
    return;
  }

  const SDL_Rect logo_bounds{18, 52, 204, 67};
  if (SDL_Texture *logo = Image(game->logo_path)) DrawTextureFit(logo, logo_bounds, false);
  DrawText(game->title, 120, 124, 17, kInk, 214, true);

  const SDL_Rect video_bounds{13, 151, 214, 120};
  Fill(renderer_, video_bounds, SDL_Color{0, 0, 0, 255});
  if (video_texture_) {
    SDL_RenderCopy(renderer_, video_texture_, nullptr, &video_bounds);
  } else {
    SDL_Texture *fallback = Image(!game->cover_path.empty() ? game->cover_path : game->logo_path);
    if (fallback) DrawTextureFit(fallback, video_bounds, true);
  }
  Stroke(renderer_, video_bounds, SDL_Color{58, 61, 65, 255});
  if (!game->developer.empty()) DrawText(game->developer, 13, 278, 12, kMuted, 214);
  const SDL_Rect details_bounds{7, 294, 226, canvas_height_ - 299};
  if (description_highlighted_) {
    Fill(renderer_, details_bounds, SDL_Color{18, 20, 23, 255});
    Stroke(renderer_, details_bounds, SDL_Color{255, 255, 255, 220});
  }
  const Uint32 now = SDL_GetTicks();
  const int max_scroll = std::max(0, static_cast<int>(
      WrappedTextLines(game->description, 214, DescriptionFontSize()).size()) - DescriptionVisibleLines());
  description_scroll_line_ = std::min(description_scroll_line_, max_scroll);
  if (!preferences_.fullscreen_grid && !sidebar_open_ && !search_open_ && !help_open_ &&
      max_scroll > 0 && SDL_TICKS_PASSED(now, description_next_at_)) {
    description_scroll_line_ = description_scroll_line_ < max_scroll ? description_scroll_line_ + 1 : 0;
    description_next_at_ = now + ((description_scroll_line_ == max_scroll || description_scroll_line_ == 0) ? 3500 : 1700);
  }
  DrawWrappedText(game->description, 13, 302, 214, DescriptionLineHeight(),
                  DescriptionVisibleLines(), DescriptionFontSize(), kInk,
                  description_scroll_line_);
  SDL_RenderSetViewport(renderer_, nullptr);
}

void GbaFrontend::RenderGrid(float chrome_hidden_progress) {
  const int columns = GridColumns();
  const float progress = std::clamp(chrome_hidden_progress, 0.0f, 1.0f);
  const int grid_x = static_cast<int>(std::lround(kGridX * (1.0f - progress)));
  const int grid_y = static_cast<int>(std::lround(kGridY * (1.0f - progress)));
  const int grid_width = static_cast<int>(std::lround(
      canvas_width_ - kGridX * (1.0f - progress)));
  const int grid_height = static_cast<int>(std::lround(
      canvas_height_ - kGridY * (1.0f - progress)));
  const int grid_inset = static_cast<int>(std::lround(
      kGridInset + (kFullscreenGridInset - kGridInset) * progress));
  const int card_size = std::max(1, (grid_width - grid_inset * 2) / columns);
  const int content_width = card_size * columns;
  const int base_x = grid_x + (grid_width - content_width) / 2;
  const int base_y = grid_y + grid_inset;
  const int start = scroll_row_ * columns;
  const int slots = (GridVisibleRows() + 1) * columns;
  const SDL_Rect grid_clip{grid_x, grid_y, grid_width, grid_height};

  if (visible_.empty()) {
    DrawText("当前列表无游戏", grid_x + grid_width / 2,
             grid_y + grid_height / 2 - 10, 20, kMuted,
             std::max(120, grid_width - grid_inset * 2), true);
    return;
  }

  auto draw_card = [&](int visible_index, const SDL_Rect &cover, bool highlighted) {
    const GbaGame &game = games_[visible_[visible_index]];
    Fill(renderer_, cover, kPanel);
    if (SDL_Texture *texture = Image(game.cover_path)) {
      DrawTextureFit(texture, cover, false);
    } else {
      DrawText("GBA", cover.x + cover.w / 2, cover.y + cover.h / 2 - 12,
               18, kMuted, cover.w - 8, true);
    }

    if (preferences_.show_cover_titles) {
      const int title_height = std::clamp(
          std::max(cover.h / 5, CoverTitleFontSize() + 8), 22, 30);
      const SDL_Rect title_bg{cover.x, cover.y + cover.h - title_height,
                              cover.w, title_height};
      Fill(renderer_, title_bg, SDL_Color{0, 0, 0, 192});
      DrawCoverTitle(game.title,
                     SDL_Rect{cover.x + 4, cover.y + cover.h - title_height + 4,
                              cover.w - 8, title_height - 4},
                     CoverTitleFontSize(), kInk, highlighted, SDL_GetTicks());
    }
    if (highlighted) {
      constexpr double kPi = 3.14159265358979323846;
      const double phase = (SDL_GetTicks() % 1400) / 1400.0 * 2.0 * kPi;
      const Uint8 alpha = static_cast<Uint8>(185 + 70 * (0.5 + 0.5 * std::sin(phase)));
      Stroke(renderer_, cover, SDL_Color{255, 255, 255, alpha});
      const SDL_Rect inner{cover.x + 1, cover.y + 1, cover.w - 2, cover.h - 2};
      Stroke(renderer_, inner, SDL_Color{255, 255, 255, static_cast<Uint8>(alpha / 2)});
    } else {
      Stroke(renderer_, cover, SDL_Color{55, 57, 60, 255});
    }
    if (game.favorite) {
      const int size = std::clamp(cover.w / 5, 18, 28);
      const int overflow = highlighted ? size / 4 : 0;
      const SDL_Rect heart{std::min(cover.x + cover.w - size + overflow, grid_clip.x + grid_clip.w - size - 2),
                           std::max(cover.y - overflow, grid_clip.y + 2), size, size};
      for (int dy = -2; dy <= 2; ++dy) {
        for (int dx = -2; dx <= 2; ++dx) {
          if (dx * dx + dy * dy <= 4)
            DrawIcon(6, SDL_Rect{heart.x + dx, heart.y + dy, size, size},
                     SDL_Color{255, 255, 255, 255});
        }
      }
      DrawIcon(6, heart, SDL_Color{240, 50, 74, 255});
    }
  };

  SDL_RenderSetClipRect(renderer_, &grid_clip);
  const Uint32 now = SDL_GetTicks();
  struct ScaledCard {
    int visible_index = -1;
    SDL_Rect cover{};
    bool highlighted = false;
  };
  std::vector<ScaledCard> scaled_cards;
  for (int slot = 0; slot < slots; ++slot) {
    const int visible_index = start + slot;
    if (visible_index >= static_cast<int>(visible_.size())) break;
    const int column = slot % columns;
    const int row = slot / columns;
    const SDL_Rect cover{base_x + column * card_size,
                         base_y + row * card_size, card_size, card_size};
    draw_card(visible_index, cover, false);

    const int game_index = visible_[visible_index];
    const bool highlighted = visible_index == selected_;
    const float fallback_scale = highlighted ? 1.15f : 1.0f;
    const float scale = CoverScale(game_index, fallback_scale, now);
    if (scale > 1.001f || highlighted) {
      const int expanded = static_cast<int>(std::lround(card_size * scale));
      const int center_x = cover.x + cover.w / 2;
      const int center_y = cover.y + cover.h / 2;
      scaled_cards.push_back({visible_index,
                              SDL_Rect{center_x - expanded / 2,
                                       center_y - expanded / 2,
                                       expanded, expanded},
                              highlighted});
    }
  }

  std::stable_sort(scaled_cards.begin(), scaled_cards.end(),
                   [](const ScaledCard &left, const ScaledCard &right) {
                     return !left.highlighted && right.highlighted;
                   });
  for (const ScaledCard &card : scaled_cards) {
    draw_card(card.visible_index, card.cover, card.highlighted);
  }
  SDL_RenderSetClipRect(renderer_, nullptr);
}

void GbaFrontend::RenderSettings() {
  Fill(renderer_, SDL_Rect{0, 0, canvas_width_, canvas_height_}, SDL_Color{11, 12, 14, 255});
  const int width = std::min(720, canvas_width_);
  const SDL_Rect viewport{(canvas_width_ - width) / 2, (canvas_height_ - 480) / 2,
                          std::max(720, canvas_width_), std::max(480, canvas_height_)};
  SDL_RenderSetViewport(renderer_, &viewport);
  constexpr int kTitleY = 25;
  constexpr int kTitleSize = 28;
  constexpr int kBrandSize = 13;
  constexpr int kVersionSize = 15;
  const std::string brand_text = "天马G ROC移植";
  const std::string version_text = "ver " + app_version_;
  int title_height = 0;
  int brand_height = 0;
  int version_height = 0;
  TTF_Font *title_font = Font(kTitleSize);
  TTF_Font *brand_font = Font(kBrandSize);
  TTF_Font *version_font = Font(kVersionSize);
  if (title_font) TTF_SizeUTF8(title_font, "设置", nullptr, &title_height);
  if (brand_font) TTF_SizeUTF8(brand_font, brand_text.c_str(), nullptr,
                               &brand_height);
  if (version_font) TTF_SizeUTF8(version_font, version_text.c_str(), nullptr,
                                 &version_height);
  const int version_y = kTitleY + std::max(0, title_height - version_height);
  DrawText("设置", 36, kTitleY, kTitleSize, kInk);
  DrawTextRight(brand_text, width - 37, version_y - brand_height,
                kBrandSize, kInk);
  DrawTextRight(version_text, width - 37, version_y, kVersionSize, kMuted);
  SDL_SetRenderDrawColor(renderer_, 72, 75, 80, 255);
  SDL_RenderDrawLine(renderer_, 36, 66, width - 37, 66);

  const std::string bgm_values[] = {"8bit", "游戏音", "静音"};
  const std::string filter_values[] = {"校色", "原色", "自定义"};
  const std::string video_values[] = {"单次", "循环"};
  const std::string grid_values[] = {"大格子", "中格子", "小格子"};
  const std::string theme_values[kGbaThemeColorCount] = {
      "金属浅蓝", "金属粉", "金属银", "黑色", "靛蓝", "黄色",
      "金属深蓝", "冰川蓝", "绿透", "灰色", "红透",
  };
  const std::string labels[kSettingsCount] = {
      "返回官方系统", "开机自动进入", "使用天马启动页", "屏幕亮度", "前端音量", "系统音量", "GBA游戏滤镜",
      "使用推荐按键配置", "背景音乐", "预览视频", "封面大小", "主题颜色", "封面标题字号", "介绍文字字号", "待机模式", "重启", "关机",
  };
  const std::string values[kSettingsCount] = {
      "", status_.autostart ? "< 开启 >" : "< 关闭 >",
      preferences_.use_pegasus_splash ? "< 使用 >" : "< 不使用 >",
      "< " + std::to_string(std::max(0, status_.brightness)) + " >",
      "< " + std::to_string(std::max(0, status_.volume)) + " >",
      preferences_.system_volume == 0 ? "< 同步 >" : "< " + std::to_string(preferences_.system_volume) + " >",
      "< " + filter_values[static_cast<int>(preferences_.filter_mode)] + " >",
      preferences_.use_recommended_controls ? "< 使用 >" : "< 不使用 >",
      "< " + bgm_values[static_cast<int>(preferences_.bgm_mode)] + " >",
      "< " + video_values[preferences_.preview_video_loop ? 1 : 0] + " >",
      "< " + grid_values[static_cast<int>(preferences_.grid_size)] + " >",
      "< " + theme_values[static_cast<int>(preferences_.theme_color)] + " >",
      "< " + std::to_string(CoverTitleFontSize()) + " >",
      "< " + std::to_string(DescriptionFontSize()) + " >",
      preferences_.super_standby ? "< 超长待机 >" : "< 默认待机 >",
      "", "",
  };

  constexpr int kRowY = 78;
  constexpr int kRowHeight = 62;
  constexpr int kVisibleRows = 6;
  for (int slot = 0; slot < kVisibleRows; ++slot) {
    const int index = settings_scroll_ + slot;
    if (index >= kSettingsCount) break;
    const SDL_Rect row{36, kRowY + slot * kRowHeight, width - 72, kRowHeight};
    if (index == settings_selected_) {
      Fill(renderer_, row, SDL_Color{42, 45, 50, 255});
      Fill(renderer_, SDL_Rect{36, row.y, 4, row.h}, kAccent);
    }
    DrawText(labels[index], 56, row.y + 19, 19, kInk, 390);
    if (index == 11) {
      const ThemeColors theme = ColorsForTheme(preferences_.theme_color);
      const SDL_Rect swatch{width - 194, row.y + 19, 22, 22};
      Fill(renderer_, swatch, theme.bar);
      Stroke(renderer_, swatch, theme.selected);
    }
    if (!values[index].empty()) {
      DrawText(values[index], width - 95, row.y + 19, 18,
               index == settings_selected_ ? kAccent : kMuted, 130, true);
    }
    SDL_SetRenderDrawColor(renderer_, 44, 47, 51, 255);
    SDL_RenderDrawLine(renderer_, 48, row.y + row.h - 1, width - 48, row.y + row.h - 1);
  }

  const int kScrollTrackX = width - 20;
  constexpr int kScrollTrackWidth = 6;
  constexpr int kScrollTrackHeight = kVisibleRows * kRowHeight;
  const int thumb_height = std::max(
      48, kScrollTrackHeight * kVisibleRows / kSettingsCount);
  const int max_scroll = std::max(1, kSettingsCount - kVisibleRows);
  const int thumb_y = kRowY +
      (kScrollTrackHeight - thumb_height) * settings_scroll_ / max_scroll;
  Fill(renderer_, SDL_Rect{kScrollTrackX, kRowY, kScrollTrackWidth,
                           kScrollTrackHeight},
       SDL_Color{43, 46, 50, 255});
  Fill(renderer_, SDL_Rect{kScrollTrackX, thumb_y, kScrollTrackWidth,
                           thumb_height},
       SDL_Color{139, 143, 149, 255});
  SDL_RenderSetViewport(renderer_, nullptr);
}

void GbaFrontend::RenderCoreMenu() {
  Fill(renderer_, SDL_Rect{0, 0, canvas_width_, canvas_height_}, SDL_Color{0, 0, 0, 96});
  const SDL_Rect viewport{(canvas_width_ - 720) / 2, (canvas_height_ - 480) / 2,
                          std::max(720, canvas_width_), std::max(480, canvas_height_)};
  SDL_RenderSetViewport(renderer_, &viewport);
  const SDL_Rect dialog{224, 102, 272, 276};
  Fill(renderer_, dialog, SDL_Color{17, 18, 20, 248});
  Stroke(renderer_, dialog, SDL_Color{112, 116, 122, 255});
  DrawText("选择游戏核心", 360, 121, 20, kInk, 240, true);

  const GbaGame *game = SelectedGame();
  const char *options[] = {
      "mGBA 核心",
      game && game->is_rumble ? "gpSP 震动核心" : "gpSP 核心",
      "VBA-M 核心",
      "VBA Next 核心",
  };
  for (int index = 0; index < 4; ++index) {
    const SDL_Rect row{244, 159 + index * 48, 232, 40};
    if (index == core_menu_selected_) {
      Fill(renderer_, row, SDL_Color{48, 51, 56, 255});
      Fill(renderer_, SDL_Rect{row.x, row.y, 4, row.h}, kAccent);
    }
    DrawText(options[index], 360, row.y + 9, 18,
             index == core_menu_selected_ ? kAccent : kInk, 210, true);
  }
  SDL_RenderSetViewport(renderer_, nullptr);
}

void GbaFrontend::RenderVersionMenu() {
  const std::vector<std::string> roms = SelectedRomOptions();
  if (roms.empty()) return;
  Fill(renderer_, SDL_Rect{0, 0, canvas_width_, canvas_height_}, SDL_Color{0, 0, 0, 112});
  const int visible_rows = std::min(kVersionMenuVisibleRows, static_cast<int>(roms.size()));
  const int dialog_height = 78 + visible_rows * 45;
  const SDL_Rect dialog{(canvas_width_ - 480) / 2, (canvas_height_ - dialog_height) / 2, 480, dialog_height};
  Fill(renderer_, dialog, SDL_Color{17, 18, 20, 250});
  Stroke(renderer_, dialog, SDL_Color{112, 116, 122, 255});
  DrawText("选择游戏版本", canvas_width_ / 2, dialog.y + 17, 20, kInk, 440, true);

  for (int slot = 0; slot < visible_rows; ++slot) {
    const int index = version_menu_scroll_ + slot;
    if (index >= static_cast<int>(roms.size())) break;
    const SDL_Rect row{(canvas_width_ - 440) / 2, dialog.y + 58 + slot * 45, 440, 38};
    if (index == version_menu_selected_) {
      Fill(renderer_, row, SDL_Color{48, 51, 56, 255});
      Fill(renderer_, SDL_Rect{row.x, row.y, 4, row.h}, kAccent);
    }
    const std::string label = fs::u8path(roms[index]).stem().u8string();
    DrawText(label, row.x + 14, row.y + 8, 17,
             index == version_menu_selected_ ? kAccent : kInk, row.w - 28);
  }
}

namespace {
constexpr int kKeyboardColumns = 12;
constexpr const char kKeyboardRows[][kKeyboardColumns + 1] = {
    "1234567890[]", "qwertyuiop-_", "asdfghjkl:/'", "zxcvbnm.,!?+"};
}

void GbaFrontend::OpenSearch() {
  EndRepeat(Action::ToggleChrome);
  sidebar_open_ = false;
  search_open_ = true;
  search_draft_ = search_query_;
  search_composition_.clear();
  keyboard_row_ = 1;
  keyboard_column_ = 0;
  held_grid_action_ = Action::None;
  SDL_StartTextInput();
  SDL_Rect input{};
  int right = 0, bottom = 0;
  const float x = 83 + (canvas_width_ - 720) / 2;
  const float y = 142 + (canvas_height_ - 480) / 2;
  SDL_RenderLogicalToWindow(renderer_, x, y, &input.x, &input.y);
  SDL_RenderLogicalToWindow(renderer_, x + 554, y + 38, &right, &bottom);
  input.w = right - input.x;
  input.h = bottom - input.y;
  SDL_SetTextInputRect(&input);
}

void GbaFrontend::HandleSearch(Action action) {
  if (action == Action::SearchDone) {
    search_query_ = search_draft_;
    search_open_ = false;
    SDL_StopTextInput();
    held_grid_action_ = Action::None;
    selected_ = scroll_row_ = 0;
    RefreshVisible();
    SelectionChanged();
  } else if (action == Action::Menu || action == Action::Back) {
    EndRepeat(Action::ToggleChrome);
    search_open_ = false;
    SDL_StopTextInput();
    held_grid_action_ = Action::None;
  } else if (action == Action::Favorite) {
    EndRepeat(Action::ToggleChrome);
    search_draft_.clear();
    search_composition_.clear();
    SDL_StopTextInput();
    SDL_StartTextInput();
  } else if (action == Action::ToggleChrome) {
    game_search::Backspace(search_draft_);
  } else if (action == Action::TabPrevious) keyboard_upper_ = !keyboard_upper_;
  else if (action == Action::QuickMenu && search_draft_.size() < 240) search_draft_ += ' ';
  else if (action == Action::Up || action == Action::Down) {
    keyboard_row_ = std::clamp(keyboard_row_ + (action == Action::Up ? -1 : 1), 0, 4);
  } else if (action == Action::Left || action == Action::Right) {
    const int step = keyboard_row_ == 4 ? 2 : 1;
    keyboard_column_ = (keyboard_column_ + kKeyboardColumns +
        (action == Action::Left ? -step : step)) % kKeyboardColumns;
  } else if (action == Action::Confirm && keyboard_row_ == 4) {
    const Action operations[] = {Action::TabPrevious, Action::QuickMenu, Action::ToggleChrome, Action::Back, Action::Favorite, Action::SearchDone};
    HandleSearch(operations[keyboard_column_ / 2]);
  } else if (action == Action::Confirm && search_draft_.size() < 240) {
    char ch = kKeyboardRows[keyboard_row_][keyboard_column_];
    search_draft_ += keyboard_upper_ ? static_cast<char>(std::toupper(static_cast<unsigned char>(ch))) : ch;
  }
}

void GbaFrontend::AdjustSetting(int index, Action action) {
  if (index == 0 && action == Action::Confirm) {
    running_ = false;
    exit_code_ = 0;
  } else if (index == 1 &&
             (action == Action::Confirm || action == Action::Left || action == Action::Right)) {
    const bool next = !services_.AutostartEnabled();
    if (services_.SetAutostart(next)) {
      status_.autostart = next;
    } else if (options_.diagnostics) {
      std::cerr << "[gba] failed to update autostart setting\n";
    }
  } else if (index == 2 &&
             (action == Action::Left || action == Action::Right)) {
    const bool next = !preferences_.use_pegasus_splash;
    if (services_.SetPegasusSplash(next) || !options_.screenshot_path.empty()) {
      preferences_.use_pegasus_splash = next;
      SavePreferences();
    } else if (options_.diagnostics) {
      std::cerr << "[gba] failed to update PegasusG splash setting\n";
    }
  } else if (index == 3 && (action == Action::Left || action == Action::Right || action == Action::Confirm)) {
    status_.brightness = services_.ChangeBrightness(action == Action::Left ? -1 : 1);
  } else if (index == 4 && (action == Action::Left || action == Action::Right || action == Action::Confirm)) {
    status_.volume = services_.ChangeVolume(action == Action::Left ? -1 : 1);
  } else if (index == 6 &&
             (action == Action::Left || action == Action::Right || action == Action::Confirm)) {
    const int direction = action == Action::Left ? -1 : 1;
    const int value = (static_cast<int>(preferences_.filter_mode) + 3 + direction) % 3;
    preferences_.filter_mode = static_cast<GbaFilterMode>(value);
    SavePreferences();
  } else if (index == 7 &&
             (action == Action::Left || action == Action::Right || action == Action::Confirm)) {
    const bool next = !preferences_.use_recommended_controls;
    if (services_.SetRecommendedControls(next) || !options_.screenshot_path.empty()) {
      preferences_.use_recommended_controls = next;
      SavePreferences();
    } else if (options_.diagnostics) {
      std::cerr << "[gba] failed to update recommended RetroArch controls\n";
    }
  } else if (index == 8 &&
             (action == Action::Left || action == Action::Right || action == Action::Confirm)) {
    const int direction = action == Action::Left ? -1 : 1;
    const int value = (static_cast<int>(preferences_.bgm_mode) + 3 + direction) % 3;
    preferences_.bgm_mode = static_cast<GbaBgmMode>(value);
    SavePreferences();
    RefreshAudio();
  } else if (index == 9 &&
             (action == Action::Left || action == Action::Right || action == Action::Confirm)) {
    preferences_.preview_video_loop = !preferences_.preview_video_loop;
    SavePreferences();
    video_.StopVideo();
    video_start_at_ = 0;
    video_version_ = 0;
    if (video_texture_) {
      SDL_DestroyTexture(video_texture_);
      video_texture_ = nullptr;
    }
    if (!options_.no_video && options_.screenshot_path.empty() &&
        !selected_video_path_.empty()) {
      video_start_at_ = SDL_GetTicks() + 300;
    }
    RefreshAudio();
  } else if (index == 10 &&
             (action == Action::Left || action == Action::Right || action == Action::Confirm)) {
    const int direction = action == Action::Left ? -1 : 1;
    const int value = (static_cast<int>(preferences_.grid_size) + 3 + direction) % 3;
    preferences_.grid_size = static_cast<GbaGridSize>(value);
    SavePreferences();
    EnsureSelectionVisible();
  } else if (index == 11 &&
             (action == Action::Left || action == Action::Right || action == Action::Confirm)) {
    const int direction = action == Action::Left ? -1 : 1;
    const int value = (static_cast<int>(preferences_.theme_color) +
                       kGbaThemeColorCount + direction) % kGbaThemeColorCount;
    preferences_.theme_color = static_cast<GbaThemeColor>(value);
    SavePreferences();
  } else if (index == 12 &&
             (action == Action::Left || action == Action::Right || action == Action::Confirm)) {
    const int direction = action == Action::Left ? -1 : 1;
    preferences_.cover_title_size_level =
        (preferences_.cover_title_size_level + kFontSizeLevelCount + direction) %
        kFontSizeLevelCount;
    SavePreferences();
  } else if (index == 13 &&
             (action == Action::Left || action == Action::Right || action == Action::Confirm)) {
    const int direction = action == Action::Left ? -1 : 1;
    preferences_.description_size_level =
        (preferences_.description_size_level + kFontSizeLevelCount + direction) %
        kFontSizeLevelCount;
    description_scroll_line_ = 0;
    SavePreferences();
  } else if (index == 5 && (action == Action::Left || action == Action::Right || action == Action::Confirm)) {
    const int direction = action == Action::Left ? -1 : 1;
    preferences_.system_volume = (preferences_.system_volume + 10 + direction) % 10;
    SavePreferences();
  } else if (index == 14 && (action == Action::Left || action == Action::Right || action == Action::Confirm)) {
    preferences_.super_standby = !preferences_.super_standby;
    SavePreferences();
  } else if (index == 15 && action == Action::Confirm) {
    video_.Stop();
    exit_code_ = kExitRestartSystem;
    running_ = false;
  } else if (index == 16 && action == Action::Confirm) {
    video_.Stop();
    exit_code_ = kExitPowerOffSystem;
    running_ = false;
  }
  if (index == 17 && (action == Action::Left || action == Action::Right || action == Action::Confirm)) {
    preferences_.show_cover_titles = !preferences_.show_cover_titles;
    SavePreferences();
  }
}

void GbaFrontend::RenderHints() {
  const char *keys[] = {"A", "B", "X", "Y", "L/R", "L2", "R2", "SE", "ST"};
  const char *labels[] = {"启动", "菜单", preferences_.fullscreen_grid ? "返回" : "全屏",
                          "快捷菜单", "分类", "换色", "搜索", "收藏", "核心"};
  constexpr int count = 9, padding = 4, gap = 4;
  const int right = canvas_width_ - 6;
  int size = 13, first = 0;
  int key_widths[count], label_widths[count], total = gap * (count - 1);
  const auto measure = [&](const char *text) {
    int width = 0;
    if (TTF_Font *font = Font(size)) TTF_SizeUTF8(font, text, &width, nullptr);
    return width;
  };
  while (true) {
    total = gap * (count - first - 1);
    for (int i = first; i < count; ++i) {
      key_widths[i] = std::max(14, measure(keys[i]) + 5);
      label_widths[i] = measure(labels[i]);
      total += key_widths[i] + 3 + label_widths[i];
    }
    if (total + padding * 2 <= right - kGridX || size == 10) break;
    if (first == 0) first = 2;
    else --size;
  }
  int x = right - padding - total;
  Fill(renderer_, SDL_Rect{x - padding, canvas_height_ - 30, total + padding * 2, 26}, SDL_Color{14, 16, 20, 215});
  for (int i = first; i < count; ++i) {
    Fill(renderer_, SDL_Rect{x, canvas_height_ - 26, key_widths[i], 18}, SDL_Color{213, 216, 222, 255});
    DrawText(keys[i], x + key_widths[i] / 2, canvas_height_ - 26, size, SDL_Color{24, 26, 30, 255}, key_widths[i], true);
    DrawText(labels[i], x + key_widths[i] + 3, canvas_height_ - 26, size, kInk);
    x += key_widths[i] + 3 + label_widths[i] + gap;
  }
  if (!search_query_.empty()) {
    const std::string label = "搜索：" + search_query_;
    int width = 0;
    if (TTF_Font *font = Font(11)) TTF_SizeUTF8(font, label.c_str(), &width, nullptr);
    width = std::min(width, right - kGridX - padding * 2);
    Fill(renderer_, SDL_Rect{right - width - padding * 2, canvas_height_ - 51, width + padding * 2, 20},
         SDL_Color{14, 16, 20, 215});
    DrawText(label, right - padding - width, canvas_height_ - 48, 11, kInk, width);
  }
}

void GbaFrontend::RenderExitDialog() {
  Fill(renderer_, SDL_Rect{0, 0, canvas_width_, canvas_height_}, SDL_Color{0, 0, 0, 135});
  const SDL_Rect viewport{(canvas_width_ - 720) / 2, (canvas_height_ - 480) / 2,
                          std::max(720, canvas_width_), std::max(480, canvas_height_)};
  SDL_RenderSetViewport(renderer_, &viewport);
  Fill(renderer_, SDL_Rect{205, 85, 310, 310}, SDL_Color{66, 69, 76, 255});
  DrawText("退出", 360, 103, 22, kInk, 270, true);
  const char *labels[] = {"返回系统", "重启", "关机", "取消"};
  for (int i = 0; i < 4; ++i) {
    const SDL_Rect row{220, 147 + i * 57, 280, 48};
    const bool selected = exit_dialog_selected_ == i;
    Fill(renderer_, row, selected
        ? (i == 2 ? SDL_Color{190, 45, 55, 255} : SDL_Color{222, 225, 233, 255})
        : SDL_Color{82, 85, 94, 255});
    DrawText(labels[i], 360, row.y + 12, 19,
             selected && i != 2 ? SDL_Color{24, 26, 30, 255} : kInk, 250, true);
  }
  SDL_RenderSetViewport(renderer_, nullptr);
}

void GbaFrontend::RenderQuickMenu() {
  const ThemeColors theme = ColorsForTheme(preferences_.theme_color);
  SDL_Color background = theme.bar;
  background.a = 228;
  const int height = kQuickSettingsCount * 31 + 12;
  const int top = canvas_height_ - 33 - height;
  Fill(renderer_, SDL_Rect{canvas_width_ - 212, top, 206, height}, background);
  Stroke(renderer_, SDL_Rect{canvas_width_ - 212, top, 206, height}, theme.selected);
  const char *bgm[] = {"8bit", "游戏音", "静音"};
  const char *grid[] = {"大", "中", "小"};
  const char *filters[] = {"校色", "原色", "自定义"};
  const char *themes[] = {"金属浅蓝", "金属粉", "金属银", "黑色", "靛蓝", "黄色",
                          "金属深蓝", "冰川蓝", "绿透", "灰色", "红透"};
  const std::string labels[] = {
      search_query_.empty() ? "搜索游戏" : search_query_,
      "屏幕亮度 " + std::to_string(std::max(0, status_.brightness)),
      "前端音量 " + std::to_string(std::max(0, status_.volume)),
      "系统音量 " + (preferences_.system_volume == 0 ? std::string("同步") : std::to_string(preferences_.system_volume)),
      std::string("背景音乐 ") + bgm[static_cast<int>(preferences_.bgm_mode)],
      std::string("预览视频 ") + (preferences_.preview_video_loop ? "循环" : "单次"),
      std::string("封面大小 ") + grid[static_cast<int>(preferences_.grid_size)],
      std::string("主题 ") + themes[static_cast<int>(preferences_.theme_color)],
      "封面字号 " + std::to_string(CoverTitleFontSize()),
      "简介字号 " + std::to_string(DescriptionFontSize()),
      std::string("封面标题 ") + (preferences_.show_cover_titles ? "显示" : "隐藏"),
      std::string("游戏滤镜 ") + filters[static_cast<int>(preferences_.filter_mode)],
  };
  for (int i = 0; i < kQuickSettingsCount; ++i) {
    const int y = top + 7 + i * 31;
    const bool selected = sidebar_selected_ == i;
    Fill(renderer_, SDL_Rect{canvas_width_ - 205, y, 192, 28},
         selected ? SDL_Color{222, 225, 233, 235} : SDL_Color{18, 20, 26, 130});
    const SDL_Color ink = selected ? SDL_Color{24, 26, 30, 255} : kInk;
    DrawText(labels[i], canvas_width_ - 109, y + 5, 14, ink, 154, true);
    if (i != 0) DrawText("<", canvas_width_ - 201, y + 4, 16, ink);
    DrawText(">", canvas_width_ - 27, y + 4, 16, ink);
  }
}

void GbaFrontend::RenderSidebar() {
  if (quick_menu_open_ && !help_open_) { RenderQuickMenu(); return; }
  Fill(renderer_, SDL_Rect{0, 0, canvas_width_, canvas_height_}, SDL_Color{0, 0, 0, 135});
  Fill(renderer_, SDL_Rect{canvas_width_ - 246, 0, 246, canvas_height_}, SDL_Color{87, 89, 94, 250});
  if (help_open_) {
    DrawText("操作帮助", canvas_width_ - 228, 28, 23, kInk);
    DrawWrappedText("方向键选择游戏\nA 启动游戏\nB 系统菜单／返回\nX 切换全屏（停止视频）\nY 侧边菜单\nL1 / R1 切换分类\nL2 主题换色\nR2 搜索游戏\nSelect 收藏／取消收藏\nStart 选择核心\n简介自动滚动并循环\n搜索支持中文、全拼和首字母", canvas_width_ - 228, 80, 210, 27, 12, 15, kInk);
    DrawText("A / B 返回", canvas_width_ - 228, canvas_height_ - 37, 14, kInk);
    return;
  }
  const char *labels[] = {"设置", "帮助", "退出"};
  for (int i = 0; i < 3; ++i) {
    const int y = canvas_height_ - 198 + i * 66;
    if (i == sidebar_selected_)
      Fill(renderer_, SDL_Rect{canvas_width_ - 246, y, 246, 66}, SDL_Color{157, 159, 167, 255});
    DrawTextRight(labels[i], canvas_width_ - 21, y + 18, 25, kInk);
  }
}

void GbaFrontend::RenderSearch() {
  Fill(renderer_, SDL_Rect{0, 0, canvas_width_, canvas_height_}, SDL_Color{0, 0, 0, 155});
  const SDL_Rect viewport{(canvas_width_ - 720) / 2, (canvas_height_ - 480) / 2,
                          std::max(720, canvas_width_), std::max(480, canvas_height_)};
  SDL_RenderSetViewport(renderer_, &viewport);
  Fill(renderer_, SDL_Rect{59, 67, 602, 348}, SDL_Color{56, 59, 66, 255});
  DrawText("搜索游戏", 84, 84, 22, kInk);
  DrawText("口袋妖怪 · kou dai yao guai / kdyg", 84, 114, 14, kInk);
  Fill(renderer_, SDL_Rect{83, 142, 554, 38}, SDL_Color{29, 32, 38, 255});
  std::string shown = search_draft_ + (search_composition_.empty() ? "_" : " [" + search_composition_ + "]");
  // Keep the insertion end visible for long queries.
  int width = 0;
  if (TTF_Font *font = Font(18)) {
    while (!shown.empty() && TTF_SizeUTF8(font, shown.c_str(), &width, nullptr) == 0 && width > 524) {
      size_t end = 1;
      while (end < shown.size() && (static_cast<unsigned char>(shown[end]) & 0xc0) == 0x80) ++end;
      shown.erase(0, end);
    }
  }
  DrawText(shown, 93, 150, 18, kInk, 530);
  for (int row = 0; row < 4; ++row) {
    const std::string keys = kKeyboardRows[row];
    for (int col = 0; col < kKeyboardColumns; ++col) {
      const bool selected = row == keyboard_row_ && col == keyboard_column_;
      const SDL_Rect box{84 + col * 46, 193 + row * 40, 41, 33};
      Fill(renderer_, box, selected ? SDL_Color{222, 225, 233, 255} : SDL_Color{82, 85, 94, 255});
      char ch = keys[col];
      if (keyboard_upper_) ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
      DrawText(std::string(1, ch), box.x + box.w / 2, box.y + 6, 17,
               selected ? SDL_Color{24, 26, 30, 255} : kInk, 35, true);
      if (selected) DrawText("A", box.x + box.w - 9, box.y + 1, 8, SDL_Color{24, 26, 30, 255});
    }
  }
  const char *buttons[] = {"L1", "Y", "X", "B", "SELECT", "START"};
  const int icons[] = {0, 1, 2, 3, 5, 4};
  for (int col = 0; col < 6; ++col) {
    const SDL_Rect box{84 + col * 92, 361, 87, 39};
    const bool selected = keyboard_row_ == 4 && keyboard_column_ / 2 == col;
    const SDL_Color ink = selected ? SDL_Color{24, 26, 30, 255} : kInk;
    Fill(renderer_, box, selected ? SDL_Color{222, 225, 233, 255} : SDL_Color{82, 85, 94, 255});
    DrawText(buttons[col], box.x + 6, box.y + 3, col < 3 ? 11 : 9, ink);
    if (selected) DrawText("A", box.x + 6, box.y + 22, 10, ink);
    DrawIcon(icons[col], SDL_Rect{box.x + 51, box.y + 6, 28, 28}, ink);
  }

  SDL_RenderSetViewport(renderer_, nullptr);
}

void GbaFrontend::DrawIcon(int index, const SDL_Rect &bounds, SDL_Color color) {
  if (!icons_texture_) {
    std::array<Uint8, ui_icons::width * ui_icons::size * 4> pixels{};
    for (size_t i = 0; i < sizeof(ui_icons::alpha); ++i) {
      pixels[i * 4] = pixels[i * 4 + 1] = pixels[i * 4 + 2] = 255;
      pixels[i * 4 + 3] = ui_icons::alpha[i];
    }
    SDL_Texture *texture = SDL_CreateTexture(renderer_, SDL_PIXELFORMAT_RGBA32,
        SDL_TEXTUREACCESS_STATIC, ui_icons::width, ui_icons::size);
    if (!texture) return;
    if (SDL_UpdateTexture(texture, nullptr, pixels.data(), ui_icons::width * 4) != 0) {
      SDL_DestroyTexture(texture);
      return;
    }
    SDL_SetTextureBlendMode(texture, SDL_BLENDMODE_BLEND);
    icons_texture_ = texture;
  }
  SDL_SetTextureColorMod(icons_texture_, color.r, color.g, color.b);
  SDL_SetTextureAlphaMod(icons_texture_, color.a);
  const SDL_Rect source{index * ui_icons::size, 0, ui_icons::size, ui_icons::size};
  SDL_RenderCopy(renderer_, icons_texture_, &source, &bounds);
}

void GbaFrontend::RenderOsd() {
  if (osd_text_.empty()) return;
  if (SDL_GetTicks() >= osd_until_) {
    osd_text_.clear();
    osd_until_ = 0;
    return;
  }
  const SDL_Rect box{(canvas_width_ - 230) / 2, (canvas_height_ - 52) / 2, 230, 52};
  Fill(renderer_, box, SDL_Color{18,20,23,235});
  Stroke(renderer_, box, SDL_Color{93,98,106,255});
  DrawText(osd_text_, canvas_width_ / 2, box.y + 16, 18, SDL_Color{255,255,255,255}, 210, true);
}

void GbaFrontend::DrawText(const std::string &text, int x, int y, int size, SDL_Color color,
                           int max_width, bool right_align) {
  if (text.empty()) return;
  TTF_Font *font = Font(size);
  if (!font) return;
  std::string display = text;
  int width = 0;
  int height = 0;
  TTF_SizeUTF8(font, display.c_str(), &width, &height);
  if (max_width > 0 && width > max_width) {
    const auto chars = Utf8Characters(display);
    display.clear();
    for (const std::string &ch : chars) {
      std::string candidate = display + ch;
      TTF_SizeUTF8(font, (candidate + "...").c_str(), &width, &height);
      if (width > max_width) break;
      display = std::move(candidate);
    }
    display += "...";
  }
  const std::string key = std::to_string(size) + ":" + std::to_string(color.r) + ":" +
                          std::to_string(color.g) + ":" + std::to_string(color.b) + ":" + display;
  auto found = text_cache_.find(key);
  if (found == text_cache_.end()) {
    const SDL_Color opaque{color.r, color.g, color.b, 255};
    SDL_Surface *surface = TTF_RenderUTF8_Blended(font, display.c_str(), opaque);
    if (!surface) return;
    TextTexture value{SDL_CreateTextureFromSurface(renderer_, surface), surface->w, surface->h};
    SDL_FreeSurface(surface);
    if (value.texture) SDL_SetTextureBlendMode(value.texture, SDL_BLENDMODE_BLEND);
    found = text_cache_.emplace(key, value).first;
  }
  const SDL_Rect target{right_align ? x - found->second.width / 2 : x, y,
                        found->second.width, found->second.height};
  SDL_SetTextureAlphaMod(found->second.texture, color.a);
  SDL_RenderCopy(renderer_, found->second.texture, nullptr, &target);
  if (color.a != 255) SDL_SetTextureAlphaMod(found->second.texture, 255);
}

void GbaFrontend::DrawCoverTitle(const std::string &text, const SDL_Rect &bounds, int size,
                                 SDL_Color color, bool highlighted, Uint32 now) {
  if (text.empty() || bounds.w <= 0 || bounds.h <= 0) return;
  TTF_Font *font = Font(size);
  if (!font) return;

  int width = 0;
  int height = 0;
  if (TTF_SizeUTF8(font, text.c_str(), &width, &height) != 0) return;
  if (!highlighted || width <= bounds.w) {
    DrawText(text, bounds.x + bounds.w / 2,
             bounds.y + std::max(0, (bounds.h - height) / 2), size, color,
             bounds.w, true);
    return;
  }

  const std::string key = std::to_string(size) + ":" + std::to_string(color.r) + ":" +
                          std::to_string(color.g) + ":" + std::to_string(color.b) + ":" + text;
  auto found = text_cache_.find(key);
  if (found == text_cache_.end()) {
    const SDL_Color opaque{color.r, color.g, color.b, 255};
    SDL_Surface *surface = TTF_RenderUTF8_Blended(font, text.c_str(), opaque);
    if (!surface) return;
    TextTexture value{SDL_CreateTextureFromSurface(renderer_, surface), surface->w, surface->h};
    SDL_FreeSurface(surface);
    if (!value.texture) return;
    SDL_SetTextureBlendMode(value.texture, SDL_BLENDMODE_BLEND);
    found = text_cache_.emplace(key, value).first;
  }

  const Uint32 elapsed = now - selected_title_started_at_;
  const float offset = elapsed > kCoverTitleMarqueeDelay
      ? (elapsed - kCoverTitleMarqueeDelay) * kCoverTitleMarqueeSpeed / 1000.0f
      : 0.0f;
  const int span = found->second.width + kCoverTitleMarqueeGap;
  const int x_offset = span > 0
      ? static_cast<int>(std::fmod(offset, static_cast<float>(span))) : 0;
  const int text_y = bounds.y + std::max(0, (bounds.h - found->second.height) / 2);

  SDL_Rect previous_clip{};
  const SDL_bool had_clip = SDL_RenderIsClipEnabled(renderer_);
  if (had_clip) SDL_RenderGetClipRect(renderer_, &previous_clip);
  SDL_Rect clip = bounds;
  if (had_clip && !SDL_IntersectRect(&previous_clip, &bounds, &clip)) return;
  SDL_RenderSetClipRect(renderer_, &clip);
  SDL_SetTextureAlphaMod(found->second.texture, color.a);
  const SDL_Rect first{bounds.x - x_offset, text_y,
                       found->second.width, found->second.height};
  const SDL_Rect second{first.x + span, text_y,
                        found->second.width, found->second.height};
  SDL_RenderCopy(renderer_, found->second.texture, nullptr, &first);
  SDL_RenderCopy(renderer_, found->second.texture, nullptr, &second);
  if (color.a != 255) SDL_SetTextureAlphaMod(found->second.texture, 255);
  SDL_RenderSetClipRect(renderer_, had_clip ? &previous_clip : nullptr);
}

void GbaFrontend::DrawTextRight(const std::string &text, int right_x, int y, int size,
                                SDL_Color color) {
  TTF_Font *font = Font(size);
  if (!font || text.empty()) return;
  int width = 0;
  if (TTF_SizeUTF8(font, text.c_str(), &width, nullptr) != 0) return;
  DrawText(text, right_x - width, y, size, color);
}

std::vector<std::string> GbaFrontend::WrappedTextLines(const std::string &text,
                                                       int width, int size) {
  const std::string cache_key = WrappedTextCacheKey(text, width, size);
  const auto cached = wrapped_text_cache_.find(cache_key);
  if (cached != wrapped_text_cache_.end()) return cached->second;

  std::vector<std::string> lines;
  TTF_Font *font = Font(size);
  if (!font) return lines;
  std::string line;
  int line_width = 0;
  const auto chars = Utf8Characters(text);
  for (const std::string &character : chars) {
    if (character == "\r") continue;
    if (character == "\n") {
      lines.push_back(line);
      line.clear();
      line_width = 0;
      continue;
    }
    int character_width = 0;
    if (TTF_SizeUTF8(font, character.c_str(), &character_width, nullptr) != 0) {
      character_width = 0;
    }
    if (line_width + character_width > width && !line.empty()) {
      lines.push_back(line);
      line = character;
      line_width = character_width;
    } else {
      line += character;
      line_width += character_width;
    }
  }
  if (!line.empty()) lines.push_back(line);
  wrapped_text_cache_.emplace(cache_key, lines);
  return lines;
}

void GbaFrontend::DrawWrappedText(const std::string &text, int x, int y, int width,
                                  int line_height, int max_lines, int size,
                                  SDL_Color color, int start_line) {
  const std::vector<std::string> lines = WrappedTextLines(text, width, size);
  const int start = std::clamp(start_line, 0, std::max(0, static_cast<int>(lines.size()) - 1));
  for (int index = 0; index < max_lines && start + index < static_cast<int>(lines.size()); ++index) {
    DrawText(lines[start + index], x, y + index * line_height, size, color, width);
  }
}

void GbaFrontend::DrawTextureFit(SDL_Texture *texture, const SDL_Rect &bounds, bool crop) {
  if (!texture) return;
  int width = 0, height = 0;
  SDL_QueryTexture(texture, nullptr, nullptr, &width, &height);
  if (width <= 0 || height <= 0) return;
  const double scale = crop ? std::max(bounds.w / static_cast<double>(width), bounds.h / static_cast<double>(height))
                            : std::min(bounds.w / static_cast<double>(width), bounds.h / static_cast<double>(height));
  const SDL_Rect target{bounds.x + (bounds.w - static_cast<int>(width * scale)) / 2,
                        bounds.y + (bounds.h - static_cast<int>(height * scale)) / 2,
                        static_cast<int>(width * scale), static_cast<int>(height * scale)};
  const bool had_clip = SDL_RenderIsClipEnabled(renderer_) == SDL_TRUE;
  SDL_Rect previous_clip{};
  SDL_RenderGetClipRect(renderer_, &previous_clip);
  SDL_Rect clip = bounds;
  if (had_clip) SDL_IntersectRect(&bounds, &previous_clip, &clip);
  SDL_RenderSetClipRect(renderer_, &clip);
  SDL_RenderCopy(renderer_, texture, nullptr, &target);
  SDL_RenderSetClipRect(renderer_, had_clip ? &previous_clip : nullptr);
}

TTF_Font *GbaFrontend::Font(int size) {
  const auto found = fonts_.find(size);
  if (found != fonts_.end()) return found->second;
  TTF_Font *font = options_.font_path.empty() ? nullptr : TTF_OpenFont(options_.font_path.c_str(), size);
  fonts_[size] = font;
  return font;
}

SDL_Texture *GbaFrontend::Image(const std::string &path) {
  if (path.empty()) return nullptr;
  const auto found = images_.find(path);
  if (found != images_.end()) {
    image_lru_.splice(image_lru_.begin(), image_lru_, found->second.lru);
    return found->second.texture;
  }

  const ResolvedImagePath resolved =
      ResolveOptimizedImagePath(path, options_.use_mini_assets);
  const fs::path source = fs::u8path(resolved.path);

  const auto alias = image_aliases_.find(path);
  if (alias != image_aliases_.end()) {
    const auto canonical = images_.find(alias->second);
    if (canonical != images_.end()) {
      image_lru_.splice(image_lru_.begin(), image_lru_, canonical->second.lru);
      ++reused_image_aliases_;
      return canonical->second.texture;
    }
    image_aliases_.erase(alias);
  }

  std::string fingerprint;
  const auto cached_fingerprint = image_fingerprint_cache_.find(path);
  if (cached_fingerprint != image_fingerprint_cache_.end()) {
    fingerprint = cached_fingerprint->second;
  } else {
    const std::string sampled = SampledFileFingerprint(source.u8string());
    if (!sampled.empty()) fingerprint = source.extension().u8string() + ':' + sampled;
    image_fingerprint_cache_[path] = fingerprint;
  }
  if (!fingerprint.empty()) {
    const auto duplicate = image_fingerprints_.find(fingerprint);
    if (duplicate != image_fingerprints_.end()) {
      const auto canonical = images_.find(duplicate->second);
      if (canonical != images_.end()) {
        image_aliases_[path] = duplicate->second;
        image_lru_.splice(image_lru_.begin(), image_lru_, canonical->second.lru);
        ++reused_image_aliases_;
        return canonical->second.texture;
      }
      image_fingerprints_.erase(duplicate);
    }
  }

  SDL_Texture *texture = IMG_LoadTexture(renderer_, source.u8string().c_str());
  if (!texture) return nullptr;
  if (resolved.optimized) ++thumbnail_image_loads_;
  else ++original_image_loads_;
  SDL_SetTextureScaleMode(texture, SDL_ScaleModeLinear);
  if (images_.size() >= kImageCacheCapacity) {
    const std::string &oldest = image_lru_.back();
    const auto old = images_.find(oldest);
    if (old != images_.end()) {
      SDL_DestroyTexture(old->second.texture);
      images_.erase(old);
    }
    image_lru_.pop_back();
  }
  image_lru_.push_front(path);
  images_.emplace(path, ImageTexture{texture, image_lru_.begin()});
  if (!fingerprint.empty()) image_fingerprints_[fingerprint] = path;
  return texture;
}

const GbaGame *GbaFrontend::SelectedGame() const {
  if (selected_ < 0 || selected_ >= static_cast<int>(visible_.size())) return nullptr;
  return &games_[visible_[selected_]];
}

GbaGame *GbaFrontend::SelectedGame() {
  if (selected_ < 0 || selected_ >= static_cast<int>(visible_.size())) return nullptr;
  return &games_[visible_[selected_]];
}

std::vector<std::string> GbaFrontend::SelectedRomOptions() const {
  const GbaGame *game = SelectedGame();
  if (!game) return {};
  std::vector<std::string> roms;
  roms.reserve(1 + game->alternate_roms.size());
  roms.push_back(game->rom_path);
  roms.insert(roms.end(), game->alternate_roms.begin(), game->alternate_roms.end());
  return roms;
}

bool GbaFrontend::LaunchGame(GbaGame &game, const std::string &rom_path) {
  game.recent_order = next_recent_order_++;
  state_.LimitRecent(&games_, kMaximumRecentGames);
  state_.Save(games_);
  if (!WriteLaunchRequest(game, rom_path)) {
    SetOsd("无法创建游戏启动请求");
    return false;
  }
  SaveUiState();
  exit_code_ = kExitLaunchGame;
  running_ = false;
  return true;
}

bool GbaFrontend::WriteLaunchRequest(const GbaGame &game, const std::string &rom_path) {
  const fs::path path = fs::u8path(options_.launch_request_path);
  std::error_code error;
  fs::create_directories(path.parent_path(), error);
  std::ofstream output(path, std::ios::trunc | std::ios::binary);
  const char *filter_modes[] = {"calibrated", "original", "custom"};
  output << rom_path << '\n' << GbaLaunchCoreName(game) << '\n'
         << filter_modes[static_cast<int>(preferences_.filter_mode)] << '\n'
         << preferences_.system_volume << '\n';
  return static_cast<bool>(output);
}

bool GbaFrontend::SaveScreenshot(const std::string &path) {
  int width = 0, height = 0;
  if (SDL_GetRendererOutputSize(renderer_, &width, &height) != 0) return false;
  SDL_Surface *surface = SDL_CreateRGBSurfaceWithFormat(0, width, height, 32, SDL_PIXELFORMAT_ARGB8888);
  if (!surface) return false;
  const bool okay = SDL_RenderReadPixels(renderer_, nullptr, SDL_PIXELFORMAT_ARGB8888,
                                         surface->pixels, surface->pitch) == 0 &&
                    IMG_SavePNG(surface, path.c_str()) == 0;
  SDL_FreeSurface(surface);
  return okay;
}

void GbaFrontend::SetOsd(std::string text, Uint32 duration) {
  osd_text_ = std::move(text);
  osd_until_ = SDL_GetTicks() + duration;
}
