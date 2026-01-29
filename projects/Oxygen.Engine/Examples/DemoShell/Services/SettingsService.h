//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <filesystem>
#include <memory>
#include <optional>
#include <shared_mutex>
#include <source_location>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <Oxygen/Base/ObserverPtr.h>

namespace oxygen::examples {

//! JSON-backed settings persistence for demo UIs.
class SettingsService {
public:
  explicit SettingsService(std::filesystem::path storage_path);
  ~SettingsService() noexcept;

  //! Creates a settings service stored alongside the calling demo source file.
  static auto CreateForDemo(std::source_location location
    = std::source_location::current()) -> std::unique_ptr<SettingsService>;

  //! Sets the process-wide default settings service.
  static auto SetDefault(oxygen::observer_ptr<SettingsService> service) -> void;

  //! Returns the process-wide default settings service.
  static auto Default() -> oxygen::observer_ptr<SettingsService>;

  //! Loads settings from disk (called automatically by constructor).
  auto Load() -> void;

  //! Saves settings to disk.
  auto Save() const -> void;

  //! Gets a 2D integer vector stored under the given key.
  auto GetVec2i(std::string_view key) const
    -> std::optional<std::pair<int, int>>;

  //! Sets a 2D integer vector stored under the given key.
  auto SetVec2i(std::string_view key, std::pair<int, int> value) -> void;

  //! Gets a float stored under the given key.
  auto GetFloat(std::string_view key) const -> std::optional<float>;

  //! Sets a float stored under the given key.
  auto SetFloat(std::string_view key, float value) -> void;

  //! Gets a string stored under the given key.
  auto GetString(std::string_view key) const -> std::optional<std::string>;

  //! Sets a string stored under the given key.
  auto SetString(std::string_view key, std::string value) -> void;

  //! Gets a boolean stored under the given key.
  auto GetBool(std::string_view key) const -> std::optional<bool>;

  //! Sets a boolean stored under the given key.
  auto SetBool(std::string_view key, bool value) -> void;

  //! Gets the storage path.
  [[nodiscard]] auto GetStoragePath() const noexcept
    -> const std::filesystem::path&
  {
    return storage_path_;
  }

private:
  static auto SplitKey(std::string_view key) -> std::vector<std::string>;
  auto FindNode(std::string_view key) const -> const void*;
  auto ResolveNode(std::string_view key) -> void*;

  mutable std::shared_mutex mutex_ {};
  std::filesystem::path storage_path_;
  bool dirty_ { false };
  bool loaded_ { false };
  struct JsonStorage;
  std::unique_ptr<JsonStorage> storage_;
};

} // namespace oxygen::examples
