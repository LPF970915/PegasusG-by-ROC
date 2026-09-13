#include "h700_services.h"

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>

#ifndef _WIN32
#include <fcntl.h>
#include <sys/ioctl.h>
#include <unistd.h>
#endif

namespace fs = std::filesystem;

namespace {

constexpr const char *kBatteryRoot = "/sys/class/power_supply/axp2202-battery";

std::string ReadText(const std::string &path) {
  std::ifstream input(path);
  std::string value;
  std::getline(input, value);
  return value;
}

std::string ShellQuote(const std::string &value) {
  std::string result = "'";
  for (char ch : value) result += ch == '\'' ? "'\\''" : std::string(1, ch);
  return result + "'";
}

bool ApplyPanelBrightness(int level) {
#ifdef _WIN32
  (void)level;
  return true;
#else
  constexpr unsigned long kPanelBrightness[10] = {
      5, 10, 20, 35, 50, 70, 100, 140, 200, 255,
  };
  const int display = open("/dev/disp", O_RDWR | O_CLOEXEC);
  if (display < 0) return false;
  unsigned long arguments[4] = {};
  arguments[1] = kPanelBrightness[std::clamp(level, 1, 10) - 1];
  const bool applied = ioctl(display, 0x102, arguments) == 0;
  close(display);
  return applied;
#endif
}

bool ApplyVolumeLevel(int level) {
#ifdef _WIN32
  (void)level;
  return true;
#else
  const int clamped = std::clamp(level, 0, 9);
  const int mixer_value = clamped == 0 ? 0 : 1 + ((clamped - 1) * 30 + 4) / 8;
  std::ostringstream command;
  command << "unset LD_PRELOAD; amixer -q -c 0 set 'lineout volume' " << mixer_value;
  if (clamped == 0) command << " && amixer -q -c 0 set SPK off";
  else command << " && amixer -q -c 0 set SPK on";
  return std::system(command.str().c_str()) == 0;
#endif
}

}  // namespace

H700Services::H700Services(std::string app_dir, std::string state_dir)
    : app_dir_(std::move(app_dir)), state_dir_(std::move(state_dir)) {}

int H700Services::ReadInt(const std::string &path, int fallback) const {
  std::ifstream input(path);
  int value = fallback;
  return input >> value ? value : fallback;
}

bool H700Services::WriteInt(const std::string &path, int value) const {
  std::ofstream output(path);
  output << value;
  return static_cast<bool>(output);
}

bool H700Services::AutostartEnabled() const {
  std::error_code error;
  return fs::is_regular_file(fs::u8path(state_dir_) / "autostart.enabled", error);
}

H700Status H700Services::ReadStatus() const {
  H700Status status;
  status.battery_percent = ReadInt(std::string(kBatteryRoot) + "/capacity", -1);
  const std::string battery_status = ReadText(std::string(kBatteryRoot) + "/status");
  status.charging = battery_status == "Charging" || battery_status == "Full";
  const fs::path brightness_state = fs::u8path(state_dir_) / "brightness.level";
  status.brightness = ReadInt(brightness_state.u8string(),
                              ReadInt(std::string(kBatteryRoot) + "/brightness", -1));
  const int lineout = ReadInt((fs::u8path(state_dir_) / "volume.level").u8string(), -1);
  status.volume = lineout;
  status.autostart = AutostartEnabled();
  return status;
}

int H700Services::ChangeBrightness(int delta) const {
  const std::string system_path = std::string(kBatteryRoot) + "/brightness";
  const fs::path state_path = fs::u8path(state_dir_) / "brightness.level";
  const int current = ReadInt(state_path.u8string(), ReadInt(system_path, 8));
  const int next = std::clamp(current + delta, 1, 10);
  // The stock frontend stores a logical level in battery sysfs but applies the
  // actual panel brightness through the Allwinner display driver's ioctl.
  if (!ApplyPanelBrightness(next)) return current;
  std::error_code error;
  fs::create_directories(state_path.parent_path(), error);
  WriteInt(system_path, next);
  return WriteInt(state_path.u8string(), next) ? next : current;
}

bool H700Services::RestoreBrightness() const {
  const std::string system_path = std::string(kBatteryRoot) + "/brightness";
  const fs::path state_path = fs::u8path(state_dir_) / "brightness.level";
  const int current = ReadInt(state_path.u8string(), 8);
  if (!ApplyPanelBrightness(current)) return false;
  std::error_code error;
  fs::create_directories(state_path.parent_path(), error);
  WriteInt(system_path, current);
  return WriteInt(state_path.u8string(), current);
}

int H700Services::ChangeVolume(int delta) const {
  const fs::path state_path = fs::u8path(state_dir_) / "volume.level";
  int current = ReadInt(state_path.u8string(), 6);
  const int next = std::clamp(current + delta, 0, 9);
  std::error_code error;
  fs::create_directories(state_path.parent_path(), error);

  if (!ApplyVolumeLevel(next)) return current;
  WriteInt(state_path.u8string(), next);
  return next;
}

bool H700Services::RestoreVolume() const {
  const fs::path state_path = fs::u8path(state_dir_) / "volume.level";
  const int current = ReadInt(state_path.u8string(), 6);
  if (!ApplyVolumeLevel(current)) return false;
  std::error_code error;
  fs::create_directories(state_path.parent_path(), error);
  return WriteInt(state_path.u8string(), current);
}

int H700Services::HallState() const {
  return ReadInt(std::string(kBatteryRoot) + "/hallkey", -1);
}

bool H700Services::Suspend(bool automatic) const {
  const char *script = "/mnt/vendor/ctrl/pwr_new.sh";
  std::error_code error;
  if (!fs::is_regular_file(script, error)) return false;
  WriteInt(std::string(kBatteryRoot) + "/os_sleep", automatic ? 0 : 16);
  const std::string command = std::string(script) + (automatic ? " auto" : "");
  return std::system(command.c_str()) == 0;
}

bool H700Services::SetAutostart(bool enabled) const {
  const fs::path helper = fs::u8path(app_dir_) / "autostart_ctl.sh";
  std::error_code error;
  if (!fs::is_regular_file(helper, error)) return false;
  const std::string command = ShellQuote(helper.u8string()) + (enabled ? " enable" : " disable");
  return std::system(command.c_str()) == 0;
}

bool H700Services::SetPegasusSplash(bool enabled) const {
  const fs::path helper = fs::u8path(app_dir_) / "tools/apply_splash.sh";
  std::error_code error;
  if (!fs::is_regular_file(helper, error)) return false;
  const std::string command = ShellQuote(helper.u8string()) +
                              (enabled ? " enable" : " disable");
  return std::system(command.c_str()) == 0;
}

bool H700Services::SetRecommendedControls(bool enabled) const {
  const fs::path helper = fs::u8path(app_dir_) / "tools/apply_recommended_controls.sh";
  std::error_code error;
  if (!fs::is_regular_file(helper, error)) return false;
  const std::string command = ShellQuote(helper.u8string()) +
                              (enabled ? " enable" : " disable");
  return std::system(command.c_str()) == 0;
}
