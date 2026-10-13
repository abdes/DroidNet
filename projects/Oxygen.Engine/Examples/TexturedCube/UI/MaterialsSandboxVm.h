//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <array>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include "DemoShell/Services/FileBrowserService.h"
#include "TexturedCube/SceneSetup.h"
#include "TexturedCube/TextureLoadingService.h"

#include <Oxygen/Base/Macros.h>
#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Core/Types/AssetKey.h>
#include <Oxygen/Core/Types/Format.h>
#include <Oxygen/Core/Types/TextureType.h>

namespace oxygen::examples::textured_cube::ui {

class MaterialsSandboxVm {
public:
  //! State for the cooked texture import UI.
  struct ImportState {
    std::array<char, 512> cooked_root {};
    std::array<char, 512> source_path {};

    // Pro Mode Settings
    enum class Usage { kAuto, kAlbedo, kNormal, kHdrEnvironment, kUi };
    Usage usage { Usage::kAuto };

    bool compress { true };
    bool compute_hash { true };

    // Advanced / Internal
    int import_kind { 0 };

    // New Tuning
    bool flip_normal_green { false };
    float exposure_ev { 0.0F };
    int bc7_quality_idx { 2 }; // Default
    int hdr_handling_idx { 1 }; // AutoTonemap
    int output_format_idx { 0 };
    bool generate_mips { true };
    int max_mip_levels { 0 };
    int mip_filter_idx { 1 };
    bool flip_y { false };
    bool force_rgba { true };
    int cube_face_size { 512 };
    int layout_idx { 0 };

    // UI State
    enum class WorkflowState { Idle, Configuring, Importing, Finished };
    WorkflowState workflow_state { WorkflowState::Idle };

    // Status
    float progress { 0.0F };
    std::string status_message;
    bool last_import_success { false };
  };

  //! One cooked texture entry for the browser list.
  struct CookedTextureEntry {
    std::uint32_t index { 0U };
    std::uint32_t width { 0U };
    std::uint32_t height { 0U };
    std::uint32_t mip_levels { 0U };
    std::uint32_t array_layers { 0U };
    std::uint64_t size_bytes { 0U };
    std::uint64_t content_hash { 0U };
    std::string name;
    oxygen::Format format { oxygen::Format::kUnknown };
    oxygen::TextureType texture_type { oxygen::TextureType::kTexture2D };
  };

  //! One cooked material entry for the browser list.
  struct CookedMaterialEntry {
    oxygen::data::AssetKey key;
    std::string virtual_path;
    std::string descriptor_relpath;
    std::uint64_t descriptor_size { 0U };
  };

  //! State for per-object texture selection.
  struct TextureSlotState {
    TextureIndexMode mode { TextureIndexMode::kFallback };
    std::uint32_t resource_index { 0U };
    oxygen::content::ResourceKey resource_key { 0U };
  };

  //! State for the demo surface material.
  struct SurfaceState {
    float metalness { 0.85F };
    float roughness { 0.12F };
    bool use_constant_base_color { false };
    glm::vec3 constant_base_color_rgb { 0.82F, 0.82F, 0.82F };
  };

  explicit MaterialsSandboxVm(
    observer_ptr<TextureLoadingService> texture_service,
    observer_ptr<oxygen::examples::FileBrowserService> file_browser = nullptr);
  ~MaterialsSandboxVm();

  OXYGEN_MAKE_NON_COPYABLE(MaterialsSandboxVm);
  OXYGEN_MAKE_NON_MOVABLE(MaterialsSandboxVm);

  // --- Commands ---

  auto RequestImport() -> void;

  // Inline Workflow Actions
  auto StartImportFlow() -> void;
  auto CancelImport() -> void;
  auto OnFileSelected(const std::filesystem::path& path) -> void;
  //! Apply smart defaults based on the usage.
  auto UpdateImportSettingsFromUsage() -> void;

  auto RequestRefresh() -> void;
  auto BrowseForSourcePath() -> void;
  auto BrowseForCookedRoot() -> void;

  //! Triggered when the user selects a texture for a specific slot.
  //! Returns true if the load was initiated.
  auto SelectTextureForSlot(uint32_t entry_index, bool is_sphere) -> bool;

  //! Triggered when the user selects a skybox texture.
  auto SelectSkybox(uint32_t entry_index,
    std::function<void(oxygen::content::ResourceKey)> on_loaded = nullptr)
    -> bool;

  auto SetOnSkyboxSelected(
    std::function<void(oxygen::content::ResourceKey)> callback) -> void;

  // --- State Access ---

  //! Returns a formatted JSON string of the texture settings, if available.
  [[nodiscard]] auto GetMetadataJson(uint32_t entry_index) const -> std::string;

  [[nodiscard]] auto GetImportState() -> ImportState& { return import_state_; }
  [[nodiscard]] auto GetImportState() const -> const ImportState&
  {
    return import_state_;
  }

  [[nodiscard]] auto GetCookedEntries() const
    -> const std::vector<CookedTextureEntry>&
  {
    return cooked_entries_;
  }

  [[nodiscard]] auto GetSphereTextureState() -> TextureSlotState&
  {
    return sphere_texture_;
  }
  [[nodiscard]] auto GetSphereTextureState() const -> const TextureSlotState&
  {
    return sphere_texture_;
  }

  [[nodiscard]] auto GetCubeTextureState() -> TextureSlotState&
  {
    return cube_texture_;
  }
  [[nodiscard]] auto GetCubeTextureState() const -> const TextureSlotState&
  {
    return cube_texture_;
  }

  [[nodiscard]] auto GetSurfaceState() -> SurfaceState&
  {
    return surface_state_;
  }
  [[nodiscard]] auto GetSurfaceState() const -> const SurfaceState&
  {
    return surface_state_;
  }

  [[nodiscard]] auto IsUseCustomMaterial() const -> bool
  {
    return use_custom_material_;
  }
  auto SetUseCustomMaterial(bool enabled) -> void;

  [[nodiscard]] auto GetCookedMaterialEntries() const
    -> const std::vector<CookedMaterialEntry>&
  {
    return cooked_material_entries_;
  }

  [[nodiscard]] auto GetSelectedCustomMaterialKey() const
    -> std::optional<oxygen::data::AssetKey>;
  auto ClearSelectedCustomMaterial() -> void;
  auto SelectCustomMaterialByEntryIndex(std::size_t entry_index) -> bool;

  [[nodiscard]] auto IsCubeRebuildNeeded() const -> bool
  {
    return cube_needs_rebuild_;
  }
  auto ClearCubeRebuildNeeded() -> void { cube_needs_rebuild_ = false; }
  auto SetCubeRebuildNeeded() -> void { cube_needs_rebuild_ = true; }

  // --- Update loop ---
  auto Update() -> void;

private:
  auto UpdateImportStatus() -> void;
  auto HandleRefresh() -> void;
  auto UpdateCookedEntries() -> void;
  auto UpdateCookedMaterialEntries(const std::filesystem::path& cooked_root)
    -> void;

  observer_ptr<TextureLoadingService> texture_service_;
  observer_ptr<oxygen::examples::FileBrowserService> file_browser_;
  std::filesystem::path content_root_;

  enum class BrowseMode { kNone, kSourcePath, kCookedRoot };
  BrowseMode browse_mode_ { BrowseMode::kNone };
  FileBrowserService::RequestId active_request_id_ { 0 };

  ImportState import_state_;
  TextureSlotState sphere_texture_;
  TextureSlotState cube_texture_;

  SurfaceState surface_state_;

  std::vector<CookedTextureEntry> cooked_entries_;
  std::vector<CookedMaterialEntry> cooked_material_entries_;
  std::optional<oxygen::data::AssetKey> selected_custom_material_key_;
  bool use_custom_material_ { false };
  bool cube_needs_rebuild_ { false };
  bool refresh_requested_ { false };

  std::function<void(oxygen::content::ResourceKey)> on_skybox_selected_;
};

} // namespace oxygen::examples::textured_cube::ui
