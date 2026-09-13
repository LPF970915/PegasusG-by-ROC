#pragma once

#include <SDL.h>
#include <SDL_ttf.h>

#include <chrono>
#include <cstdint>
#include <list>
#include <string>
#include <unordered_map>
#include <vector>

#include "gba_model.h"
#include "gba_preferences.h"
#include "gba_state.h"
#include "gba_ui_state.h"
#include "h700_services.h"
#include "video_preview.h"

struct GbaFrontendOptions {
  int width = 720;
  int height = 480;
  std::vector<std::string> content_roots;
  std::string state_dir;
  std::string app_dir;
  std::string launch_request_path;
  std::string mod_overrides_path;
  std::string font_path;
  std::string screenshot_path;
  std::string screenshot_action;
  int screenshot_delay_ms = -1;
  bool diagnostics = false;
  bool no_video = false;
  bool restore_ui = false;
  bool reuse_bgm_track = false;
  bool use_mini_assets = false;
};

class GbaFrontend {
  friend struct GbaFrontendInteractionTest;
 public:
  explicit GbaFrontend(GbaFrontendOptions options);
  ~GbaFrontend();

  bool Initialize();
  int Run();

 private:
  enum class Action {
    None, Up, Down, Left, Right, Confirm, Back, Favorite,
    ToggleTitles, ToggleChrome, DescriptionUp, DescriptionDown,
    JumpUpHundred, JumpDownHundred, GridSmaller, GridLarger,
    QuickTheme, NextBgm, TabPrevious, TabNext, CoreMenu,
    Menu, QuickMenu, Search, SearchDone, MenuPress, MenuRelease, VolumeDown, VolumeUp, Power,
  };

  struct TextTexture {
    SDL_Texture *texture = nullptr;
    int width = 0;
    int height = 0;
  };

  struct ImageTexture {
    SDL_Texture *texture = nullptr;
    std::list<std::string>::iterator lru;
  };

  struct CoverScaleAnimation {
    float from = 1.0f;
    float to = 1.0f;
    Uint32 started_at = 0;
  };

  Action Translate(const SDL_Event &event);
  bool InitializeRuntime();
  void DestroyRuntime();
  void HandleHallState(int current);
  void OpenEvdevInput();
  void PollEvdevInput();
  void BeginRepeat(Action action);
  void EndRepeat(Action action);
  void PollHeldActions();
  void Handle(Action action);
  void StartCoverScaleAnimation(int game_index, float fallback_from, float target,
                                Uint32 now);
  float CoverScale(int game_index, float fallback, Uint32 now);
  void RefreshVisible();
  void PrewarmNextGameAsset();
  void SelectionChanged();
  void SelectEightBitTrack();
  void AdvanceEightBitTrack();
  void RefreshAudio();
  void SavePreferences();
  int GridColumns() const;
  int GridCardSize() const;
  int GridVisibleRows() const;
  float ChromeHiddenProgress(Uint32 now) const;
  int CoverTitleFontSize() const;
  int DescriptionFontSize() const;
  int DescriptionLineHeight() const;
  int DescriptionVisibleLines() const;
  void EnsureSelectionVisible();
  void EnsureSettingsVisible();
  void EnsureVersionMenuVisible();
  void PollStatus();
  void PollHall();
  void RestoreUiState();
  void SaveUiState() const;
  void UpdateVideoTexture();
  void Render();
  void RenderTopBar(int y_offset);
  void RenderGameInfo(int x_offset);
  void RenderGrid(float chrome_hidden_progress);
  void RenderSettings();
  void RenderCoreMenu();
  void RenderVersionMenu();
  void RenderOsd();
  void RenderHints();
  void DrawIcon(int index, const SDL_Rect &bounds, SDL_Color color);
  void RenderSidebar();
  void RenderExitDialog();
  void RenderQuickMenu();
  void AdjustSetting(int index, Action action);
  void RenderSearch();
  void HandleSearch(Action action);
  void OpenSearch();
  void DrawText(const std::string &text, int x, int y, int size, SDL_Color color,
                int max_width = 0, bool right_align = false);
  void DrawCoverTitle(const std::string &text, const SDL_Rect &bounds, int size,
                      SDL_Color color, bool highlighted, Uint32 now);
  void DrawTextRight(const std::string &text, int right_x, int y, int size,
                     SDL_Color color);
  void DrawWrappedText(const std::string &text, int x, int y, int width, int line_height,
                       int max_lines, int size, SDL_Color color, int start_line = 0);
  std::vector<std::string> WrappedTextLines(const std::string &text, int width, int size);
  void DrawTextureFit(SDL_Texture *texture, const SDL_Rect &bounds, bool crop);
  TTF_Font *Font(int size);
  SDL_Texture *Image(const std::string &path);
  const GbaGame *SelectedGame() const;
  GbaGame *SelectedGame();
  bool LaunchGame(GbaGame &game, const std::string &rom_path);
  std::vector<std::string> SelectedRomOptions() const;
  bool WriteLaunchRequest(const GbaGame &game, const std::string &rom_path);
  bool SaveScreenshot(const std::string &path);
  void SetOsd(std::string text, Uint32 duration = 1600);

  GbaFrontendOptions options_;
  int canvas_width_ = 720;
  int canvas_height_ = 480;
  SDL_Window *window_ = nullptr;
  SDL_Renderer *renderer_ = nullptr;
  bool initialized_ = false;
  bool running_ = true;
  int exit_code_ = 0;
  std::chrono::steady_clock::time_point startup_started_;
  std::vector<GbaGame> games_;
  PegasusScanReport scan_report_;
  GbaStateStore state_;
  GbaUiStateStore ui_state_;
  GbaPreferencesStore preferences_store_;
  GbaPreferences preferences_;
  H700Services services_;
  H700Status status_;
  Uint32 next_status_poll_ = 0;
  Uint32 next_hall_poll_ = 0;
  int hall_state_ = -1;
  std::uint64_t next_recent_order_ = 1;

  int active_tab_ = 1;
  int selected_ = 0;
  int scroll_row_ = 0;
  int description_scroll_line_ = 0;
  bool description_highlighted_ = false;
  std::vector<int> visible_;
  bool settings_open_ = false;
  bool sidebar_open_ = false;
  bool exit_dialog_open_ = false;
  int exit_dialog_selected_ = 3;
  bool quick_menu_open_ = false;
  bool help_open_ = false;
  int sidebar_selected_ = 0;
  bool search_open_ = false;
  bool keyboard_upper_ = false;
  int keyboard_row_ = 1;
  int keyboard_column_ = 0;
  std::string search_query_;
  std::string search_draft_;
  std::string search_composition_;
  std::vector<std::string> search_keys_;
  bool left_trigger_held_ = false;
  bool right_trigger_held_ = false;
  Uint32 description_next_at_ = 0;
  int settings_selected_ = 0;
  int settings_scroll_ = 0;
  bool core_menu_open_ = false;
  int core_menu_selected_ = 0;
  bool version_menu_open_ = false;
  int version_menu_selected_ = 0;
  int version_menu_scroll_ = 0;
  std::string osd_text_;
  Uint32 osd_until_ = 0;
  Uint32 volume_hint_until_ = 0;
  Uint32 selected_title_started_at_ = 0;
  std::unordered_map<int, CoverScaleAnimation> cover_scale_animations_;
  int tab_transition_from_ = -1;
  int tab_transition_direction_ = 0;
  Uint32 tab_transition_started_at_ = 0;
  bool tab_transition_pending_start_ = false;
  float chrome_animation_from_ = 0.0f;
  float chrome_animation_to_ = 0.0f;
  Uint32 chrome_animation_started_at_ = 0;

  SDL_GameController *controller_ = nullptr;
  SDL_Joystick *joystick_ = nullptr;
  int axis_x_ = 0;
  int axis_y_ = 0;
  std::vector<int> evdev_input_fds_;
  bool has_evdev_gamepad_ = false;
  bool menu_button_held_ = false;
  bool menu_chord_used_ = false;
  Action held_grid_action_ = Action::None;
  bool backspace_held_ = false;
  Uint32 backspace_started_at_ = 0;
  Uint32 next_backspace_at_ = 0;
  Action held_description_action_ = Action::None;
  Uint32 next_grid_repeat_at_ = 0;
  Uint32 next_description_repeat_at_ = 0;
  Uint32 last_volume_action_at_ = 0;
  Uint32 last_interaction_at_ = 0;
  Action last_volume_action_ = Action::None;
  std::unordered_map<int, TTF_Font *> fonts_;
  std::unordered_map<std::string, TextTexture> text_cache_;
  std::unordered_map<std::string, std::vector<std::string>> wrapped_text_cache_;
  std::unordered_map<std::string, ImageTexture> images_;
  std::unordered_map<std::string, std::string> image_aliases_;
  std::unordered_map<std::string, std::string> image_fingerprints_;
  std::unordered_map<std::string, std::string> image_fingerprint_cache_;
  std::list<std::string> image_lru_;
  int thumbnail_image_loads_ = 0;
  int original_image_loads_ = 0;
  int reused_image_aliases_ = 0;
  int prewarmed_images_ = 0;
  int prewarmed_descriptions_ = 0;
  VideoPreview video_;
  SDL_Texture *video_texture_ = nullptr;
  SDL_Texture *icons_texture_ = nullptr;
  std::uint64_t video_version_ = 0;
  std::string selected_video_path_;
  std::string selected_eight_bit_track_;
  std::string app_version_ = "x.xx";
  std::vector<std::string> eight_bit_tracks_;
  size_t selected_eight_bit_index_ = 0;
  Uint32 video_start_at_ = 0;
  std::string restored_game_id_;
  int restored_scroll_row_ = -1;
};
