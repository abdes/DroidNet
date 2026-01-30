//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <string>

#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Content/Import/ImportOptions.h>
#include <Oxygen/Content/Import/LooseCookedLayout.h>

namespace oxygen::examples {

class SettingsService;

//! Grouped settings for the Content Loader workflow.
struct ContentExplorerSettings {
  std::filesystem::path model_root;
  bool include_fbx { true };
  bool include_glb { true };
  bool include_gltf { true };
  bool auto_load_on_import { true };
  bool auto_dump_texture_memory { true };
  int auto_dump_delay_frames { 180 };
  int dump_top_n { 20 };
};

//! Service responsible for persisting content loader related UI settings.
class ContentSettingsService {
public:
  ContentSettingsService() = default;
  virtual ~ContentSettingsService() = default;

  // --- Explorer & Workflow ---
  [[nodiscard]] virtual auto GetExplorerSettings() const -> ContentExplorerSettings;
  virtual auto SetExplorerSettings(const ContentExplorerSettings& settings) -> void;

  // --- Import Profile ---
  [[nodiscard]] virtual auto GetImportOptions() const -> content::import::ImportOptions;
  virtual auto SetImportOptions(const content::import::ImportOptions& options) -> void;

  [[nodiscard]] virtual auto GetTextureTuning() const -> content::import::ImportOptions::TextureTuning;
  virtual auto SetTextureTuning(const content::import::ImportOptions::TextureTuning& tuning) -> void;

  // --- Layout ---
  [[nodiscard]] virtual auto GetDefaultLayout() const -> content::import::LooseCookedLayout;
  virtual auto SetDefaultLayout(const content::import::LooseCookedLayout& layout) -> void;

  // --- Paths ---
  [[nodiscard]] virtual auto GetLastCookedOutputDirectory() const -> std::string;
  virtual auto SetLastCookedOutputDirectory(const std::string& path) -> void;

  //! Returns the current settings epoch for cache invalidation.
  [[nodiscard]] auto GetEpoch() const noexcept -> std::uint64_t;

protected:
  [[nodiscard]] auto ResolveSettings() const noexcept -> observer_ptr<SettingsService>;
  mutable std::atomic<std::uint64_t> epoch_ { 0 };
};

} // namespace oxygen::examples
