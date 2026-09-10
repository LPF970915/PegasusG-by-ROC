#include "gba_state.h"

#include <algorithm>
#include <charconv>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace fs = std::filesystem;

GbaStateStore::GbaStateStore(std::string path) : path_(std::move(path)) {}

void GbaStateStore::Load(std::vector<GbaGame> *games) {
  if (!games) return;
  std::ifstream input(fs::u8path(path_));
  std::unordered_map<std::string, GbaGameState> states;
  std::string line;
  while (std::getline(input, line)) {
    if (line.empty() || line[0] == '#') continue;
    std::istringstream row(line);
    std::string id;
    std::string favorite;
    std::string recent;
    std::string core;
    std::string core_overridden;
    if (!std::getline(row, id, '\t') || !std::getline(row, favorite, '\t') ||
        !std::getline(row, recent, '\t')) {
      continue;
    }
    std::uint64_t recent_order = 0;
    const auto parsed = std::from_chars(recent.data(), recent.data() + recent.size(),
                                        recent_order);
    if (parsed.ec == std::errc() && parsed.ptr == recent.data() + recent.size()) {
      const bool has_core = static_cast<bool>(std::getline(row, core, '\t')) && !core.empty();
      const bool has_override = static_cast<bool>(std::getline(row, core_overridden, '\t'));
      states[id] = GbaGameState{favorite == "1", recent_order,
                                GbaCoreFromStorageName(core),
                                has_core, has_override && core_overridden == "1",
                                has_core && !has_override};
      std::string favorite_order;
      if (std::getline(row, favorite_order, '\t')) {
        std::uint64_t order = 0;
        const auto parsed_order = std::from_chars(favorite_order.data(),
            favorite_order.data() + favorite_order.size(), order);
        if (parsed_order.ec == std::errc() &&
            parsed_order.ptr == favorite_order.data() + favorite_order.size())
          states[id].favorite_order = order;
      }
    }
  }
  for (GbaGame &game : *games) {
    const auto found = states.find(game.id);
    if (found == states.end()) continue;
    game.favorite = found->second.favorite;
    game.favorite_order = game.favorite ? found->second.favorite_order : 0;
    game.recent_order = found->second.recent_order;
    if (found->second.has_core) {
      const GbaCore legacy_default = game.is_rumble ? GbaCore::Gpsp : GbaCore::Mgba;
      const bool overridden = found->second.legacy_core
          ? found->second.core != legacy_default
          : found->second.core_overridden;
      if (overridden) {
        game.core = found->second.core;
        game.core_overridden = true;
      }
    }
  }
}

bool GbaStateStore::Save(const std::vector<GbaGame> &games) const {
  const fs::path path = fs::u8path(path_);
  std::error_code error;
  fs::create_directories(path.parent_path(), error);
  const fs::path temporary = path.string() + ".tmp";
  {
    std::ofstream output(temporary, std::ios::trunc);
    if (!output) return false;
    output << "# id\tfavorite\trecent_order\tcore\tcore_overridden\tfavorite_order\n";
    for (const GbaGame &game : games) {
      if (!game.favorite && game.recent_order == 0 && !game.core_overridden) continue;
      output << game.id << '\t' << (game.favorite ? 1 : 0) << '\t'
             << game.recent_order << '\t'
             << GbaCoreStorageName(game.core) << '\t'
             << (game.core_overridden ? 1 : 0) << '\t'
             << game.favorite_order << '\n';
    }
    if (!output) return false;
  }
  fs::rename(temporary, path, error);
  if (!error) return true;
  fs::remove(path, error);
  error.clear();
  fs::rename(temporary, path, error);
  return !error;
}

bool GbaStateStore::LimitRecent(std::vector<GbaGame> *games, std::size_t maximum) const {
  if (!games) return false;
  std::vector<GbaGame *> recent;
  for (GbaGame &game : *games) {
    if (game.recent_order != 0) recent.push_back(&game);
  }
  std::sort(recent.begin(), recent.end(), [](const GbaGame *left, const GbaGame *right) {
    return left->recent_order > right->recent_order;
  });
  bool changed = false;
  for (std::size_t index = maximum; index < recent.size(); ++index) {
    recent[index]->recent_order = 0;
    changed = true;
  }
  return changed;
}

std::uint64_t GbaStateStore::NextRecentOrder(const std::vector<GbaGame> &games) const {
  std::uint64_t value = 0;
  for (const GbaGame &game : games) value = std::max(value, game.recent_order);
  return value + 1;
}
