//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <array>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <fstream>
#include <memory>
#include <mutex>
#include <optional>
#include <shared_mutex>
#include <source_location>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "DemoShell/Services/SettingsService.h"
#include <nlohmann/json.hpp>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Base/ObserverPtr.h>

namespace oxygen::examples {

struct SettingsService::JsonStorage {
  nlohmann::json data = nlohmann::json::object();
};

namespace {

  auto MakeStoragePathFromLocation(const std::source_location& location)
    -> std::filesystem::path
  {
    std::filesystem::path source_path(location.file_name());
    const auto directory = source_path.parent_path();
    return directory / "demo_settings.json";
  }

  struct DefaultState {
    std::unique_ptr<SettingsService> owned { nullptr };
  };

  auto GetDefaultState() -> auto&
  {
    static DefaultState state;
    return state;
  }

  //! Settings no code reads any more. Loading drops them, so a settings file
  //! does not carry dead values forever.
  constexpr std::array kRetiredKeys {
    std::string_view { "async_demo.profiler_open" },
    std::string_view { "camera.fly_move_speed" },
    std::string_view { "content.explorer.auto_dump_delay" },
    std::string_view { "content.explorer.auto_dump_tex_mem" },
    std::string_view { "content.explorer.dump_top_n" },
    std::string_view { "diagnostics.shader_debug_mode" },
    std::string_view { "ground_grid.enabled" },
    // The host's toolbar variables live in the console archive.
    std::string_view { "host" },
    std::string_view { "light_culling.visualization_mode" },
    std::string_view { "rendering.atmosphere_blue_noise" },
    std::string_view { "rendering.debug_mode" },
    std::string_view { "rendering.view_mode" },
    std::string_view { "ui.stats.show_budget_stats" },
    std::string_view { "ui.stats.show_engine_timing" },
    std::string_view { "ui.stats.show_fps" },
    std::string_view { "ui.stats.show_frame_timing_detail" },
  };

  //! Erases the dotted `key` and the objects it leaves empty; true when the
  //! key was there.
  auto EraseKey(nlohmann::json& node, const std::string_view key) -> bool
  {
    if (!node.is_object()) {
      return false;
    }
    const auto dot = key.find('.');
    const auto head = std::string(key.substr(0, dot));
    if (dot == std::string_view::npos) {
      return node.erase(head) > 0U;
    }
    const auto child = node.find(head);
    if (child == node.end() || !EraseKey(*child, key.substr(dot + 1U))) {
      return false;
    }
    if (child->is_object() && child->empty()) {
      node.erase(child);
    }
    return true;
  }

} // namespace

SettingsService::SettingsService(
  // NOLINTNEXTLINE(bugprone-easily-swappable-parameters) - distinct roles
  std::filesystem::path storage_path,
  const std::filesystem::path& portable_root)
  : storage_path_(std::move(storage_path))
  , portable_root_(portable_root.empty()
        ? std::filesystem::path {}
        : std::filesystem::absolute(portable_root).lexically_normal())
  , storage_(std::make_unique<JsonStorage>())
{
  Load();
}

SettingsService::~SettingsService() noexcept
{
  try {
    bool should_save = false;
    {
      std::shared_lock lock(mutex_);
      should_save = dirty_ && persistence_enabled_;
    }
    if (should_save) {
      Save();
    }
  } catch (const std::exception& ex) {
    LOG_F(ERROR, "SettingsService: destructor Save failed: {}", ex.what());
  } catch (...) {
    LOG_F(ERROR, "SettingsService: destructor Save failed with unknown error");
  }
}

auto SettingsService::CreateForDemo(std::source_location location)
  -> std::unique_ptr<SettingsService>
{
  return std::make_unique<SettingsService>(
    MakeStoragePathFromLocation(location));
}

auto SettingsService::ResolveDefault(std::source_location location)
  -> observer_ptr<SettingsService>
{
  auto& state = GetDefaultState();
  if (!state.owned) {
    state.owned = CreateForDemo(location);
  }
  return observer_ptr { state.owned.get() };
}

auto SettingsService::InitializeForDemoApp(std::filesystem::path storage_path,
  const std::filesystem::path& portable_root) -> void
{
  auto& state = GetDefaultState();
  if (state.owned) {
    throw std::logic_error("Demo settings were already initialized");
  }
  state.owned
    = std::make_unique<SettingsService>(std::move(storage_path), portable_root);
}

auto SettingsService::EncodePath(const std::filesystem::path& path) const
  -> std::filesystem::path
{
  if (portable_root_.empty() || path.empty()) {
    return path;
  }
  auto absolute = std::filesystem::absolute(path).lexically_normal();
  auto relative = absolute.lexically_relative(portable_root_);
  if (!relative.empty() && *relative.begin() != "..") {
    return relative;
  }
  return absolute;
}

auto SettingsService::DecodePath(const std::filesystem::path& path) const
  -> std::filesystem::path
{
  if (portable_root_.empty() || path.empty() || path.is_absolute()) {
    return path;
  }
  return (portable_root_ / path).lexically_normal();
}

auto SettingsService::Load() -> void
{
  std::unique_lock lock(mutex_);
  dirty_ = false;
  storage_->data = nlohmann::json::object();

  std::ifstream input(storage_path_);
  if (!input.is_open()) {
    loaded_ = true;
    return;
  }

  try {
    input >> storage_->data;
  } catch (const nlohmann::json::exception& error) {
    // Saving would replace the file, so its loss must not be silent.
    LOG_F(WARNING,
      "SettingsService: '{}' is not valid JSON ({}); starting from defaults "
      "and overwriting it on the next save",
      storage_path_.string(), error.what());
    storage_->data = nlohmann::json::object();
  }
  for (const auto key : kRetiredKeys) {
    if (EraseKey(storage_->data, key)) {
      LOG_F(INFO, "SettingsService: removed retired setting '{}' from '{}'",
        key, storage_path_.string());
      dirty_ = true;
    }
  }
  loaded_ = true;
}

auto SettingsService::SetPersistenceEnabled(const bool enabled) -> void
{
  std::unique_lock lock(mutex_);
  persistence_enabled_ = enabled;
}

auto SettingsService::Save() const -> void
{
  nlohmann::json snapshot;
  {
    std::shared_lock lock(mutex_);
    CHECK_F(loaded_, "SettingsService: settings not loaded");
    if (!persistence_enabled_) {
      return;
    }
    snapshot = storage_->data;
  }

  std::ofstream output(storage_path_);
  if (!output.is_open()) {
    return;
  }

  output << snapshot.dump(2) << '\n';
}

auto SettingsService::SplitKey(std::string_view key) -> std::vector<std::string>
{
  std::vector<std::string> tokens;
  std::string current;
  for (const char ch : key) {
    if (ch == '.') {
      if (!current.empty()) {
        tokens.push_back(current);
        current.clear();
      }
      continue;
    }
    current.push_back(ch);
  }
  if (!current.empty()) {
    tokens.push_back(current);
  }
  return tokens;
}

auto SettingsService::FindNode(std::string_view key) const -> const void*
{
  const auto tokens = SplitKey(key);
  const nlohmann::json* current = &storage_->data;
  for (const auto& token : tokens) {
    if (!current->is_object()) {
      return nullptr;
    }
    const auto it = current->find(token);
    if (it == current->end()) {
      return nullptr;
    }
    current = &(*it);
  }
  return current;
}

auto SettingsService::ResolveNode(std::string_view key) -> void*
{
  const auto tokens = SplitKey(key);
  nlohmann::json* current = &storage_->data;
  for (const auto& token : tokens) {
    if (!current->is_object()) {
      *current = nlohmann::json::object();
    }
    current = &(*current)[token];
  }
  return current;
}

auto SettingsService::GetVec2i(std::string_view key) const
  -> std::optional<std::pair<int, int>>
{
  std::shared_lock lock(mutex_);
  CHECK_F(loaded_, "SettingsService: settings not loaded");
  const auto* node = static_cast<const nlohmann::json*>(FindNode(key));
  if (node == nullptr || !node->is_array() || node->size() != 2) {
    return std::nullopt;
  }

  const auto& x = (*node)[0];
  const auto& y = (*node)[1];
  if (!x.is_number_integer() || !y.is_number_integer()) {
    return std::nullopt;
  }

  return std::pair { x.get<int>(), y.get<int>() };
}

auto SettingsService::SetVec2i(std::string_view key, std::pair<int, int> value)
  -> void
{
  std::unique_lock lock(mutex_);
  CHECK_F(loaded_, "SettingsService: settings not loaded");
  auto* node = static_cast<nlohmann::json*>(ResolveNode(key));
  *node = nlohmann::json::array({ value.first, value.second });
  dirty_ = true;
}

auto SettingsService::GetFloat(std::string_view key) const
  -> std::optional<float>
{
  std::shared_lock lock(mutex_);
  CHECK_F(loaded_, "SettingsService: settings not loaded");
  const auto* node = static_cast<const nlohmann::json*>(FindNode(key));
  if (node == nullptr || !node->is_number()) {
    return std::nullopt;
  }

  return node->get<float>();
}

auto SettingsService::SetFloat(std::string_view key, float value) -> void
{
  std::unique_lock lock(mutex_);
  CHECK_F(loaded_, "SettingsService: settings not loaded");
  auto* node = static_cast<nlohmann::json*>(ResolveNode(key));
  *node = value;
  dirty_ = true;
}

auto SettingsService::GetString(std::string_view key) const
  -> std::optional<std::string>
{
  std::shared_lock lock(mutex_);
  CHECK_F(loaded_, "SettingsService: settings not loaded");
  const auto* node = static_cast<const nlohmann::json*>(FindNode(key));
  if (node == nullptr || !node->is_string()) {
    return std::nullopt;
  }

  return node->get<std::string>();
}

auto SettingsService::SetString(std::string_view key, std::string value) -> void
{
  std::unique_lock lock(mutex_);
  CHECK_F(loaded_, "SettingsService: settings not loaded");
  auto* node = static_cast<nlohmann::json*>(ResolveNode(key));
  *node = std::move(value);
  dirty_ = true;
}

auto SettingsService::GetInt(std::string_view key) const
  -> std::optional<std::int64_t>
{
  std::shared_lock lock(mutex_);
  CHECK_F(loaded_, "SettingsService: settings not loaded");
  const auto* node = static_cast<const nlohmann::json*>(FindNode(key));
  if (node == nullptr || !node->is_number_integer()) {
    return std::nullopt;
  }
  return node->get<std::int64_t>();
}

auto SettingsService::SetInt(std::string_view key, const std::int64_t value)
  -> void
{
  std::unique_lock lock(mutex_);
  CHECK_F(loaded_, "SettingsService: settings not loaded");
  auto* node = static_cast<nlohmann::json*>(ResolveNode(key));
  *node = value;
  dirty_ = true;
}

auto SettingsService::GetChildKeys(std::string_view key) const
  -> std::vector<std::string>
{
  std::shared_lock lock(mutex_);
  CHECK_F(loaded_, "SettingsService: settings not loaded");
  const auto* node = static_cast<const nlohmann::json*>(FindNode(key));
  auto keys = std::vector<std::string> {};
  if (node == nullptr || !node->is_object()) {
    return keys;
  }
  keys.reserve(node->size());
  for (const auto& [name, value] : node->items()) {
    static_cast<void>(value);
    keys.push_back(name);
  }
  return keys;
}

auto SettingsService::GetBool(std::string_view key) const -> std::optional<bool>
{
  std::shared_lock lock(mutex_);
  CHECK_F(loaded_, "SettingsService: settings not loaded");
  const auto* node = static_cast<const nlohmann::json*>(FindNode(key));
  if (node == nullptr || !node->is_boolean()) {
    return std::nullopt;
  }

  return node->get<bool>();
}

auto SettingsService::SetBool(std::string_view key, bool value) -> void
{
  std::unique_lock lock(mutex_);
  CHECK_F(loaded_, "SettingsService: settings not loaded");
  auto* node = static_cast<nlohmann::json*>(ResolveNode(key));
  *node = value; // JSON boolean
  dirty_ = true;
}

auto SettingsService::Contains(std::string_view key) const -> bool
{
  std::shared_lock lock(mutex_);
  CHECK_F(loaded_, "SettingsService: settings not loaded");
  return FindNode(key) != nullptr;
}

auto SettingsService::Remove(std::string_view key) -> bool
{
  std::unique_lock lock(mutex_);
  CHECK_F(loaded_, "SettingsService: settings not loaded");

  const auto tokens = SplitKey(key);
  if (tokens.empty()) {
    return false;
  }

  nlohmann::json* current = &storage_->data;
  for (std::size_t i = 0; i + 1 < tokens.size(); ++i) {
    if (!current->is_object()) {
      return false;
    }
    const auto it = current->find(tokens[i]);
    if (it == current->end()) {
      return false;
    }
    current = &(*it);
  }

  if (!current->is_object()) {
    return false;
  }

  const auto erased = current->erase(tokens.back());
  if (erased == 0) {
    return false;
  }

  dirty_ = true;
  return true;
}

} // namespace oxygen::examples
