#pragma once

#include <cstdint>
#include <string>
#include <vector>

enum class GbaCore {
  Mgba = 0,
  Gpsp = 1,
  Vbam = 2,
  VbaNext = 3,
};

inline const char *GbaCoreStorageName(GbaCore core) {
  switch (core) {
    case GbaCore::Gpsp: return "gpsp";
    case GbaCore::Vbam: return "vbam";
    case GbaCore::VbaNext: return "vba_next";
    default: return "mgba";
  }
}

inline GbaCore GbaCoreFromStorageName(const std::string &name) {
  if (name == "gpsp") return GbaCore::Gpsp;
  if (name == "vbam") return GbaCore::Vbam;
  if (name == "vba_next") return GbaCore::VbaNext;
  return GbaCore::Mgba;
}

struct GbaGame {
  std::string id;
  std::string title;
  std::string developer;
  std::string description;
  std::string sort_key;
  std::string rom_path;
  std::vector<std::string> alternate_roms;
  std::string cover_path;
  std::string logo_path;
  std::string video_path;
  std::string metadata_path;
  bool is_mod = false;
  bool is_rumble = false;
  bool favorite = false;
  std::uint64_t favorite_order = 0;
  std::uint64_t recent_order = 0;
  GbaCore default_core = GbaCore::Mgba;
  GbaCore core = GbaCore::Mgba;
  bool core_overridden = false;
};

inline const char *GbaLaunchCoreName(const GbaGame &game) {
  switch (game.core) {
    case GbaCore::Gpsp: return game.is_rumble ? "gpsp_rumble" : "gpsp";
    case GbaCore::Vbam: return "vbam";
    case GbaCore::VbaNext: return "vba_next";
    default: return "mgba";
  }
}

struct PegasusScanReport {
  std::vector<GbaGame> games;
  int metadata_files = 0;
  int parsed_entries = 0;
  int missing_rom_entries = 0;
  int duplicate_rom_entries = 0;
  int unreferenced_roms = 0;
  std::vector<std::string> warnings;
};
