#include "gba_state.h"

#include <cassert>
#include <filesystem>
#include <fstream>
#include <vector>

namespace fs = std::filesystem;

int main() {
  const fs::path root = fs::temp_directory_path() / "roc_gba_state_test";
  const fs::path path = root / "games.tsv";
  fs::remove_all(root);

  GbaStateStore store(path.u8string());
  std::vector<GbaGame> games(5);
  games[0].id = "game-a";
  games[0].default_core = GbaCore::Gpsp;
  games[0].core = GbaCore::Mgba;
  games[0].core_overridden = true;
  games[1].id = "game-b";
  games[1].favorite = true;
  games[1].favorite_order = 9;
  games[1].recent_order = 42;
  games[2].id = "game-rumble";
  games[2].is_rumble = true;
  games[2].default_core = GbaCore::Gpsp;
  games[2].core = GbaCore::Gpsp;
  games[3].id = "game-vbam";
  games[3].default_core = GbaCore::Mgba;
  games[3].core = GbaCore::Vbam;
  games[3].core_overridden = true;
  games[4].id = "game-vba-next";
  games[4].default_core = GbaCore::Mgba;
  games[4].core = GbaCore::VbaNext;
  games[4].core_overridden = true;
  assert(store.Save(games));
  {
    std::ifstream saved(path);
    const std::string contents((std::istreambuf_iterator<char>(saved)),
                               std::istreambuf_iterator<char>());
    assert(contents.find("game-rumble") == std::string::npos);
  }

  std::vector<GbaGame> loaded(5);
  loaded[0].id = "game-a";
  loaded[0].default_core = GbaCore::Gpsp;
  loaded[0].core = GbaCore::Gpsp;
  loaded[1].id = "game-b";
  loaded[2].id = "game-rumble";
  loaded[2].is_rumble = true;
  loaded[2].default_core = GbaCore::Gpsp;
  loaded[2].core = GbaCore::Gpsp;
  loaded[3].id = "game-vbam";
  loaded[4].id = "game-vba-next";
  store.Load(&loaded);
  assert(loaded[0].core == GbaCore::Mgba);
  assert(loaded[0].core_overridden);
  assert(std::string(GbaLaunchCoreName(loaded[0])) == "mgba");
  assert(!loaded[0].favorite);
  assert(loaded[0].recent_order == 0);
  assert(loaded[1].core == GbaCore::Mgba);
  assert(loaded[1].favorite);
  assert(loaded[1].favorite_order == 9);
  assert(loaded[1].recent_order == 42);
  assert(loaded[2].core == GbaCore::Gpsp);
  assert(std::string(GbaLaunchCoreName(loaded[2])) == "gpsp_rumble");
  assert(loaded[3].core == GbaCore::Vbam);
  assert(std::string(GbaLaunchCoreName(loaded[3])) == "vbam");
  assert(loaded[4].core == GbaCore::VbaNext);
  assert(std::string(GbaLaunchCoreName(loaded[4])) == "vba_next");

  std::ofstream(path, std::ios::trunc) << "game-a\t1\t7\n";
  loaded[0].favorite = false;
  loaded[0].recent_order = 0;
  loaded[0].core = GbaCore::Gpsp;
  loaded[0].core_overridden = false;
  store.Load(&loaded);
  assert(loaded[0].favorite);
  assert(loaded[0].favorite_order == 0);
  assert(loaded[0].recent_order == 7);
  assert(loaded[0].core == GbaCore::Gpsp);
  assert(!loaded[0].core_overridden);

  std::ofstream(path, std::ios::trunc) << "game-a\t0\t8\tmgba\n";
  loaded[0].core = GbaCore::Gpsp;
  loaded[0].core_overridden = false;
  store.Load(&loaded);
  assert(loaded[0].recent_order == 8);
  assert(loaded[0].core == GbaCore::Gpsp);
  assert(!loaded[0].core_overridden);

  std::ofstream(path, std::ios::trunc) << "game-a\t0\t9\tmgba\t1\n";
  store.Load(&loaded);
  assert(loaded[0].core == GbaCore::Mgba);
  assert(loaded[0].core_overridden);

  std::ofstream(path, std::ios::trunc) << "game-rumble\t0\t0\tmgba\n";
  store.Load(&loaded);
  assert(loaded[2].core == GbaCore::Mgba);
  assert(loaded[2].core_overridden);
  assert(std::string(GbaLaunchCoreName(loaded[2])) == "mgba");

  std::vector<GbaGame> recent(105);
  for (std::size_t index = 0; index < recent.size(); ++index) {
    recent[index].recent_order = index + 1;
  }
  assert(store.LimitRecent(&recent, 100));
  int recent_count = 0;
  for (const GbaGame &game : recent) {
    if (game.recent_order != 0) ++recent_count;
  }
  assert(recent_count == 100);

  fs::remove_all(root);
  return 0;
}
