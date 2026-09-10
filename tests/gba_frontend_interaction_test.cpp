#include "gba_frontend.h"
#include "game_search.h"
#include <cassert>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#ifndef _WIN32
#include <fcntl.h>
#include <linux/input.h>
#include <unistd.h>
#endif

struct GbaFrontendInteractionTest {
  static void Run() {
    using A = GbaFrontend::Action;
    GbaFrontendOptions options;
    options.state_dir = "build/interaction-state";
    options.app_dir = ".";
    options.screenshot_path = "build/interaction.png";
    options.no_video = true;
    options.launch_request_path = "build/interaction-state/launch.request";
    if (const char *font = std::getenv("PEGASUSG_FONT")) options.font_path = font;
    GbaFrontend app(options);
    assert(app.Initialize());
    app.preferences_ = GbaPreferences{};
    app.games_.clear(); app.search_keys_.clear();
    for (int i = 0; i < 24; ++i) {
      GbaGame game;
      game.id = std::to_string(i);
      game.title = i == 0 ? "口袋妖怪" : "光明之魂" + std::to_string(i);
      for (int j = 0; j < 30; ++j) game.description += "玩家可以选择角色，在冒险中探索世界。";
      app.search_keys_.push_back(game_search::Key(game.title));
      app.games_.push_back(game);
    }
    app.preferences_.fullscreen_grid = false;
    app.chrome_animation_from_ = app.chrome_animation_to_ = 0;
    app.active_tab_ = 1;
    app.RefreshVisible(); app.SelectionChanged();
    app.Render(); assert(app.SaveScreenshot("build/review-main.png"));
    app.Handle(A::Favorite); assert(app.SelectedGame()->favorite);
    app.preferences_.show_cover_titles = false;
    app.Render(); assert(app.SaveScreenshot("build/review-favourite.png"));
    std::vector<Uint8> pixels(480 * 350 * 4);
    const SDL_Rect covers{240, 45, 480, 350};
    const auto heart_pixels = [&]() {
      assert(SDL_RenderReadPixels(app.renderer_, &covers, SDL_PIXELFORMAT_RGBA32,
                                  pixels.data(), 480 * 4) == 0);
      int red = 0;
      for (size_t i = 0; i < pixels.size(); i += 4)
        if (pixels[i] > 200 && pixels[i + 1] < 100 && pixels[i + 2] < 130) ++red;
      return red;
    };
    assert(heart_pixels() > 0);
    app.Handle(A::Favorite); assert(!app.SelectedGame()->favorite);
    app.Render(); assert(heart_pixels() == 0);
    app.preferences_.show_cover_titles = true;
    app.selected_ = 18; app.EnsureSelectionVisible();
    app.Handle(A::Favorite);
    assert(app.SelectedGame()->id == "18" && app.selected_ == 0 && app.scroll_row_ == 0);
    app.selected_ = 13; assert(app.SelectedGame()->id == "12");
    app.Handle(A::Favorite);
    assert(app.SelectedGame()->id == "12" && app.selected_ == 1);
    app.selected_ = 0; app.Handle(A::Favorite);
    assert(app.SelectedGame()->id == "18" && app.selected_ == 18 && app.scroll_row_ > 0);
    app.selected_ = 0; app.Handle(A::Favorite);
    assert(app.SelectedGame()->id == "12" && app.selected_ == 12);
    app.Handle(A::Favorite);
    app.active_tab_ = 4; app.RefreshVisible();
    app.Handle(A::Favorite);
    assert(app.active_tab_ == 1 && app.SelectedGame()->id == "12" && app.selected_ == 12);
    app.selected_ = app.scroll_row_ = 0;
    app.RefreshVisible(); app.SelectionChanged();

    SDL_Event event{};
    event.type = SDL_CONTROLLERBUTTONDOWN;
    event.cbutton.button = SDL_CONTROLLER_BUTTON_B;
    app.Handle(app.Translate(event));
    assert(app.sidebar_open_ && !app.settings_open_);
    const int selected = app.selected_;
    app.Handle(A::Down); assert(app.selected_ == selected);
    app.Render(); assert(app.SaveScreenshot("build/review-system-menu.png"));
    app.Handle(A::Confirm); assert(app.help_open_);
    app.Render(); assert(app.SaveScreenshot("build/review-help.png"));
    app.Handle(A::Back); assert(!app.help_open_ && app.sidebar_open_);
    app.Handle(A::Up); app.Handle(A::Confirm);
    assert(app.settings_open_ && !app.sidebar_open_);
    app.Handle(A::Back);
    event.cbutton.button = SDL_CONTROLLER_BUTTON_Y;
    app.Handle(app.Translate(event));
    assert(app.sidebar_open_ && app.quick_menu_open_);
    assert(app.preferences_.system_volume == 0);
    app.sidebar_selected_ = 3;
    app.Handle(A::Right); assert(app.preferences_.system_volume == 1);
    app.AdjustSetting(5, A::Left); assert(app.preferences_.system_volume == 0);
    app.settings_open_ = true;
    app.settings_selected_ = 5; app.settings_scroll_ = 3;
    app.Render(); assert(app.SaveScreenshot("build/review-volume-settings.png"));
    app.settings_open_ = false;
    app.preferences_.system_volume = 9;
    assert(app.WriteLaunchRequest(*app.SelectedGame(), "/mnt/mmc/Roms/GBA/test.gba"));
    std::ifstream request(options.launch_request_path);
    std::string line;
    for (int i = 0; i < 4; ++i) std::getline(request, line);
    assert(line == "9");
    app.preferences_.system_volume = 0;
    app.sidebar_selected_ = 0;
    app.Render(); assert(app.SaveScreenshot("build/review-quick-menu.png"));
    const auto quick_theme = app.preferences_.theme_color;
    app.Handle(A::QuickTheme);
    assert(app.sidebar_open_ && app.quick_menu_open_ && app.preferences_.theme_color != quick_theme);
    app.Render(); assert(app.SaveScreenshot("build/review-quick-menu-color.png"));
    app.Handle(A::Up); assert(app.sidebar_selected_ == 11);
    const auto filter = app.preferences_.filter_mode;
    app.Handle(A::Right); assert(app.preferences_.filter_mode != filter);
    app.Handle(A::Left); assert(app.preferences_.filter_mode == filter);
    app.sidebar_selected_ = 10;
    app.Handle(A::Confirm); assert(!app.preferences_.show_cover_titles);
    app.Handle(A::Left); assert(app.preferences_.show_cover_titles);
    app.sidebar_selected_ = 6;
    const auto grid = app.preferences_.grid_size;
    app.Handle(A::Right); assert(app.preferences_.grid_size != grid);
    app.Handle(A::Left); assert(app.preferences_.grid_size == grid);
    assert(app.selected_ == selected && !app.settings_open_);
    app.sidebar_selected_ = 0;
    app.Handle(A::Confirm); assert(app.search_open_ && !app.sidebar_open_);
    app.Handle(A::Menu); assert(!app.search_open_);
    app.Handle(A::QuickMenu); app.Handle(A::Search);
    assert(app.search_open_ && !app.sidebar_open_);
    app.Handle(A::Menu);
    app.Handle(A::Back); assert(app.sidebar_open_ && !app.quick_menu_open_);
    app.Handle(A::Back);
    event.type = SDL_CONTROLLERAXISMOTION;
    event.caxis.axis = SDL_CONTROLLER_AXIS_TRIGGERRIGHT;
    event.caxis.value = 20000;
    app.Handle(app.Translate(event)); assert(app.search_open_);
    app.search_draft_ = "kept";
    assert(app.Translate(event) == A::None);
    assert(app.search_draft_ == "kept");
    event.caxis.value = 0; assert(app.Translate(event) == A::None);
    app.search_draft_.clear();
    app.Handle(A::Confirm); assert(app.search_draft_ == "q");
    app.Handle(A::TabPrevious); app.Handle(A::Confirm); assert(app.search_draft_ == "qQ");
    app.Handle(A::QuickMenu); assert(app.search_draft_ == "qQ ");
    app.search_draft_ = "口袋妖怪"; app.Handle(A::ToggleChrome); assert(app.search_draft_ == "口袋妖");
    app.search_draft_ = "口袋妖怪 一个很长的游戏名字";
    app.search_composition_ = "kou";
    const auto search_theme = app.preferences_.theme_color;
    app.Handle(A::Favorite);
    assert(app.search_open_ && app.search_draft_.empty() && app.search_composition_.empty());
    assert(app.preferences_.theme_color == search_theme);
    app.search_draft_ = "口袋妖怪1234567890";
    event.type = SDL_CONTROLLERBUTTONDOWN; event.cbutton.button = SDL_CONTROLLER_BUTTON_X;
    app.Handle(app.Translate(event));
    assert(app.backspace_held_ && app.search_draft_ == "口袋妖怪123456789");
    app.next_backspace_at_ = 0; app.PollHeldActions();
    assert(app.search_draft_ == "口袋妖怪12345678");
    const Uint32 slow = app.next_backspace_at_ - SDL_GetTicks();
    app.backspace_started_at_ = SDL_GetTicks() - 2000;
    app.next_backspace_at_ = 0; app.PollHeldActions();
    assert(app.next_backspace_at_ - SDL_GetTicks() < slow);
    app.search_draft_ = "怪";
    app.next_backspace_at_ = 0; app.PollHeldActions();
    app.next_backspace_at_ = 0; app.PollHeldActions();
    assert(app.search_draft_.empty() && app.search_open_);
    event.type = SDL_CONTROLLERBUTTONUP;
    app.Translate(event); assert(!app.backspace_held_);
    app.search_draft_ = "unchanged";
    app.next_backspace_at_ = 0; app.PollHeldActions();
    assert(app.search_draft_ == "unchanged");
    app.search_draft_ = "another long name";
    app.keyboard_row_ = 4; app.keyboard_column_ = 4;
    app.Handle(A::Confirm); assert(app.search_open_ && app.search_draft_.empty());
    app.Handle(A::Confirm); assert(app.search_open_);
    app.keyboard_row_ = app.keyboard_column_ = 0;
    app.search_draft_ = "kdyg";
    app.Render(); assert(app.SaveScreenshot("build/review-search.png"));
    assert(app.icons_texture_ != nullptr);
    SDL_Texture *atlas = app.icons_texture_;
    app.keyboard_row_ = 4; app.keyboard_column_ = 5;
    app.Render(); assert(app.icons_texture_ == atlas);
    assert(app.SaveScreenshot("build/review-search-selected.png"));
    event.type = SDL_CONTROLLERBUTTONDOWN;
    event.cbutton.button = SDL_CONTROLLER_BUTTON_START;
    app.Handle(app.Translate(event));
    assert(!app.search_open_ && app.visible_.size() == 1 && app.SelectedGame()->title == "口袋妖怪");
    app.Render(); assert(app.SaveScreenshot("build/review-results.png"));
    const int result_selection = app.selected_;
    app.OpenSearch(); app.search_draft_ = "no match";
    event.type = SDL_CONTROLLERBUTTONDOWN; event.cbutton.button = SDL_CONTROLLER_BUTTON_B;
    app.Handle(app.Translate(event));
    assert(!app.search_open_ && app.visible_.size() == 1 && app.selected_ == result_selection);
    app.OpenSearch(); app.search_draft_.clear(); app.Handle(A::ToggleChrome);
    assert(app.search_open_);
    app.search_draft_ = "cancel with keyboard";
    app.keyboard_row_ = 4; app.keyboard_column_ = 3; app.Handle(A::Confirm);
    assert(!app.search_open_ && app.search_query_ == "kdyg");
    assert(app.search_query_ == "kdyg");
    app.OpenSearch(); app.search_draft_ = "no match"; app.Handle(A::SearchDone);
    assert(app.visible_.empty()); app.Render();
    app.Handle(A::Search); app.search_draft_.clear();
    // The action row can be reached and activated entirely with D-pad + A.
    app.Handle(A::Down); app.Handle(A::Down); app.Handle(A::Down);
    assert(app.keyboard_row_ == 4);
    app.Handle(A::Left); assert(app.keyboard_column_ == 5);
    app.Handle(A::Confirm);
    assert(app.search_query_.empty() && app.visible_.size() == 24);
    const auto theme = app.preferences_.theme_color;
    event.type = SDL_CONTROLLERAXISMOTION;
    event.caxis.axis = SDL_CONTROLLER_AXIS_TRIGGERLEFT;
    event.caxis.value = 20000;
    app.Handle(app.Translate(event));
    assert(app.preferences_.theme_color != theme);
    assert(app.Translate(event) == A::None);
    event.caxis.value = 0; assert(app.Translate(event) == A::None);
    app.video_start_at_ = SDL_GetTicks() + 300;
    app.selected_video_path_ = "pending-preview.mp4";
    event.type = SDL_CONTROLLERBUTTONDOWN;
    event.cbutton.button = SDL_CONTROLLER_BUTTON_X;
    app.Handle(app.Translate(event));
    assert(app.preferences_.fullscreen_grid && app.video_start_at_ == 0 && app.selected_video_path_.empty());
    app.Handle(A::Right);
    assert(app.video_start_at_ == 0 && app.selected_video_path_.empty());
    app.chrome_animation_from_ = app.chrome_animation_to_ = 1;
    app.Render(); assert(app.SaveScreenshot("build/review-fullscreen.png"));
    app.Handle(app.Translate(event)); assert(!app.preferences_.fullscreen_grid);
    app.description_next_at_ = 0; app.RenderGameInfo(0);
    assert(app.description_scroll_line_ == 1);
    const int last = static_cast<int>(app.WrappedTextLines(app.SelectedGame()->description, 214, app.DescriptionFontSize()).size()) - app.DescriptionVisibleLines();
    app.description_scroll_line_ = last; app.description_next_at_ = 0;
    app.RenderGameInfo(0); assert(app.description_scroll_line_ == 0);
    app.Handle(A::Back); app.description_next_at_ = 0;
    app.RenderGameInfo(0); assert(app.description_scroll_line_ == 0);
    app.Handle(A::Down); app.Handle(A::Down); app.Handle(A::Confirm);
    assert(app.exit_dialog_open_ && app.exit_dialog_selected_ == 3 && app.running_);
    app.Render(); assert(app.SaveScreenshot("build/review-exit-dialog.png"));
    app.Handle(A::Confirm); assert(!app.exit_dialog_open_ && app.running_);
    app.Handle(A::Confirm); app.Handle(A::Back);
    assert(!app.exit_dialog_open_ && app.running_);
    app.Handle(A::Confirm); app.Handle(A::Up);
    app.Render(); assert(app.SaveScreenshot("build/review-exit-shutdown.png"));
    Uint8 shutdown_pixel[4] = {};
    const SDL_Rect shutdown_sample{225, 266, 1, 1};
    assert(SDL_RenderReadPixels(app.renderer_, &shutdown_sample, SDL_PIXELFORMAT_RGBA32,
                                shutdown_pixel, 4) == 0);
    assert(shutdown_pixel[0] > 150 && shutdown_pixel[1] < 80 && shutdown_pixel[2] < 80);
    app.Handle(A::Confirm);
    assert(!app.running_ && app.exit_code_ == 24);
    app.running_ = true;
    app.Handle(A::Confirm); app.Handle(A::Up); app.Handle(A::Up); app.Handle(A::Confirm);
    assert(!app.running_ && app.exit_code_ == 23);
    app.running_ = true;
    app.Handle(A::Confirm); app.Handle(A::Down); app.Handle(A::Confirm);
    assert(!app.running_ && app.exit_code_ == 0);
    app.running_ = true;
    app.hall_state_ = -1;
    app.HandleHallState(-1); app.HandleHallState(0);
    assert(app.running_);
    app.HandleHallState(1); app.HandleHallState(1); app.HandleHallState(-1);
    assert(app.running_ && app.hall_state_ == 1);
    const std::string before_suspend = app.SelectedGame()->id;
    app.HandleHallState(0);
    assert(!app.running_ && app.exit_code_ == 22);
    app.SaveUiState();
    GbaUiState saved;
    assert(app.ui_state_.Load(&saved) && saved.selected_game_id == before_suspend);
    assert(saved.active_tab == app.active_tab_ && saved.scroll_row == app.scroll_row_);
    app.running_ = true; app.HandleHallState(1);
    assert(app.running_);
    app.AdjustSetting(14, A::Right);
    assert(app.preferences_.super_standby);
    GbaPreferences standby_saved;
    assert(app.preferences_store_.Load(&standby_saved) && standby_saved.super_standby);
    app.HandleHallState(0);
    assert(!app.running_ && app.exit_code_ == 25);
    app.running_ = true;
    app.settings_open_ = true; app.settings_selected_ = 14; app.EnsureSettingsVisible();
    app.Render(); assert(app.SaveScreenshot("build/review-super-standby.png"));
    app.AdjustSetting(14, A::Left);
    assert(!app.preferences_.super_standby);
    app.Render(); assert(app.SaveScreenshot("build/review-default-standby.png"));
    app.settings_open_ = false;
    app.HandleHallState(1); app.HandleHallState(0);
    assert(!app.running_ && app.exit_code_ == 22);
    app.running_ = true;
    app.Handle(A::Power); assert(!app.running_ && app.exit_code_ == 21);
    app.running_ = true;
#ifndef _WIN32
    // Exercise the actual H700 evdev path using recorded button codes.
    app.sidebar_open_ = false;
    int input[2]; assert(pipe(input) == 0);
    assert(fcntl(input[0], F_SETFL, O_NONBLOCK) == 0);
    app.evdev_input_fds_.push_back(input[0]);
    const auto press = [&](int code) {
      input_event raw{}; raw.type = EV_KEY; raw.code = code; raw.value = 1;
      assert(write(input[1], &raw, sizeof(raw)) == sizeof(raw));
      app.PollEvdevInput();
    };
    press(BTN_NORTH); assert(app.preferences_.fullscreen_grid); // physical X
    press(BTN_NORTH); assert(!app.preferences_.fullscreen_grid);
    press(BTN_C); assert(app.sidebar_open_); // physical Y
    press(BTN_EAST); assert(!app.sidebar_open_);
    const auto before = app.preferences_.theme_color;
    press(BTN_SELECT); assert(app.preferences_.theme_color != before); // physical L2
    press(BTN_START); assert(app.search_open_); // physical R2
    app.search_draft_ = "cleared"; press(BTN_TL); // physical Select clears
    assert(app.search_open_ && app.search_draft_.empty());
    app.Handle(A::Menu);
    press(BTN_START); assert(app.search_open_);
    app.search_draft_ = "kdyg";
    press(BTN_TR); assert(!app.search_open_ && app.visible_.size() == 1); // physical Start
    close(input[1]); // the frontend owns and closes the read fd
#endif
    app.DestroyRuntime();
    assert(app.icons_texture_ == nullptr);
    assert(app.InitializeRuntime());
    app.RenderSearch();
    assert(app.icons_texture_ != nullptr); // recreate after renderer loss
  }
};
int main() {
  SDL_setenv("SDL_VIDEODRIVER", "dummy", 1);
  GbaFrontendInteractionTest::Run();
  std::cout << "frontend interaction tests passed\n";
}
