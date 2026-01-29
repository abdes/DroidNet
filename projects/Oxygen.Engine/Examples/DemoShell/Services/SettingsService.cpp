//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <fstream>
#include <mutex>
#include <shared_mutex>
#include <stdexcept>

#include <nlohmann/json.hpp>

#include <Oxygen/Base/Logging.h>

#include "DemoShell/Services/SettingsService.h"

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

} // namespace

static oxygen::observer_ptr<SettingsService> g_default_settings { nullptr };

SettingsService::SettingsService(std::filesystem::path storage_path)
  : storage_path_(std::move(storage_path))
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
      should_save = dirty_;
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

auto SettingsService::SetDefault(oxygen::observer_ptr<SettingsService> service)
  -> void
{
  CHECK_NOTNULL_F(
    service.get(), "SettingsService::SetDefault requires a valid service");
  CHECK_F(
    service->loaded_, "SettingsService::SetDefault requires loaded settings");
  g_default_settings = service;
}

auto SettingsService::Default() -> oxygen::observer_ptr<SettingsService>
{
  return g_default_settings;
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
  } catch (const nlohmann::json::exception&) {
    storage_->data = nlohmann::json::object();
  }
  loaded_ = true;
}

auto SettingsService::Save() const -> void
{
  nlohmann::json snapshot;
  {
    std::shared_lock lock(mutex_);
    CHECK_F(loaded_, "SettingsService: settings not loaded");
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
    current = &((*current)[token]);
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

  return std::pair<int, int> { x.get<int>(), y.get<int>() };
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

} // namespace oxygen::examples
