#pragma once

#include <cstdint>
#include <cstddef>
#include <string>
#include <unordered_map>
#include <vector>

#include "gba_model.h"

struct GbaGameState {
  bool favorite = false;
  std::uint64_t recent_order = 0;
  GbaCore core = GbaCore::Mgba;
  bool has_core = false;
  bool core_overridden = false;
  bool legacy_core = false;
  std::uint64_t favorite_order = 0;
};

class GbaStateStore {
 public:
  explicit GbaStateStore(std::string path);

  void Load(std::vector<GbaGame> *games);
  bool Save(const std::vector<GbaGame> &games) const;
  bool LimitRecent(std::vector<GbaGame> *games, std::size_t maximum) const;
  std::uint64_t NextRecentOrder(const std::vector<GbaGame> &games) const;

 private:
  std::string path_;
};
