//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include <Oxygen/Base/Macros.h>
#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Core/Types/Format.h>
#include <Oxygen/Core/Types/TextureType.h>

#include "TexturedCube/SceneSetup.h"
#include "TexturedCube/TextureLoadingService.h"

namespace oxygen::examples {
class FileBrowserService;
}

namespace oxygen::examples::textured_cube::ui {

class TextureBrowserVm {
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
    float exposure_ev { 0.0f };
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
    float progress { 0.0f };
    std::string status_message {};
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
    std::string name {};
    oxygen::Format format { oxygen::Format::kUnknown };
    oxygen::TextureType texture_type { oxygen::TextureType::kTexture2D };
  };

  //! State for per-object texture selection.
  struct TextureSlotState {
    SceneSetup::TextureIndexMode mode {
      SceneSetup::TextureIndexMode::kFallback
    };
    std::uint32_t resource_index { 0U };
    oxygen::content::ResourceKey resource_key { 0U };
  };

  //! UV orientation fix mode.
  enum class OrientationFixMode : std::uint8_t {
    kNormalizeTextureOnUpload = 0,
    kNormalizeUvInTransform = 1,
    kNone = 2,
  };

  //! Image origin convention.
  enum class ImageOrigin : std::uint8_t {
    kTopLeft = 0,
    kBottomLeft = 1,
  };

  //! UV origin convention.
  enum class UvOrigin : std::uint8_t {
    kBottomLeft = 0,
    kTopLeft = 1,
  };

  //! State for UV transformation UI.
  struct UvState {
    glm::vec2 scale { 1.0f, 1.0f };
    glm::vec2 offset { 0.0f, 0.0f };
    UvOrigin uv_origin { UvOrigin::kBottomLeft };
    ImageOrigin image_origin { ImageOrigin::kTopLeft };
    OrientationFixMode fix_mode {
      OrientationFixMode::kNormalizeTextureOnUpload
    };
    bool extra_flip_u { false };
    bool extra_flip_v { false };
  };

  //! State for the demo surface material.
  struct SurfaceState {
    float metalness { 0.85f };
    float roughness { 0.12f };
    bool use_constant_base_color { false };
    glm::vec3 constant_base_color_rgb { 0.82f, 0.82f, 0.82f };
  };

  explicit TextureBrowserVm(observer_ptr<TextureLoadingService> texture_service,
    observer_ptr<oxygen::examples::FileBrowserService> file_browser = nullptr);
  ~TextureBrowserVm();

  OXYGEN_MAKE_NON_COPYABLE(TextureBrowserVm);
  OXYGEN_MAKE_NON_MOVABLE(TextureBrowserVm);

  // --- Commands ---

  void RequestImport();

  // Inline Workflow Actions
  void StartImportFlow();
  void CancelImport();
  void OnFileSelected(const std::filesystem::path& path);
  void
  UpdateImportSettingsFromUsage(); // Apply smart defaults based on Usage enum

  void RequestRefresh();
  void BrowseForSourcePath();
  void BrowseForCookedRoot();

  //! Triggered when the user selects a texture for a specific slot.
  //! Returns true if the load was initiated.
  bool SelectTextureForSlot(uint32_t entry_index, bool is_sphere);

  //! Triggered when the user selects a skybox texture.
  bool SelectSkybox(uint32_t entry_index,
    std::function<void(oxygen::content::ResourceKey)> on_loaded = nullptr);

  void SetOnSkyboxSelected(
    std::function<void(oxygen::content::ResourceKey)> callback);

  // --- State Access ---

  //! Returns a formatted JSON string of the texture settings, if available.
  [[nodiscard]] std::string GetMetadataJson(uint32_t entry_index) const;

  ImportState& GetImportState() { return import_state_; }
  const ImportState& GetImportState() const { return import_state_; }

  const std::vector<CookedTextureEntry>& GetCookedEntries() const
  {
    return cooked_entries_;
  }

  TextureSlotState& GetSphereTextureState() { return sphere_texture_; }
  const TextureSlotState& GetSphereTextureState() const
  {
    return sphere_texture_;
  }

  TextureSlotState& GetCubeTextureState() { return cube_texture_; }
  const TextureSlotState& GetCubeTextureState() const { return cube_texture_; }

  UvState& GetUvState() { return uv_state_; }
  const UvState& GetUvState() const { return uv_state_; }

  SurfaceState& GetSurfaceState() { return surface_state_; }
  const SurfaceState& GetSurfaceState() const { return surface_state_; }

  [[nodiscard]] std::pair<glm::vec2, glm::vec2> GetEffectiveUvTransform() const;

  [[nodiscard]] bool IsCubeRebuildNeeded() const { return cube_needs_rebuild_; }
  void ClearCubeRebuildNeeded() { cube_needs_rebuild_ = false; }
  void SetCubeRebuildNeeded() { cube_needs_rebuild_ = true; }

  // --- Update loop ---
  void Update();

private:
  void UpdateImportStatus();
  void HandleRefresh();
  void UpdateCookedEntries();

  observer_ptr<TextureLoadingService> texture_service_;
  observer_ptr<oxygen::examples::FileBrowserService> file_browser_;

  enum class BrowseMode { kNone, kSourcePath, kCookedRoot };
  BrowseMode browse_mode_ { BrowseMode::kNone };

  ImportState import_state_;
  TextureSlotState sphere_texture_;
  TextureSlotState cube_texture_;

  SurfaceState surface_state_;
  UvState uv_state_;

  std::vector<CookedTextureEntry> cooked_entries_;
  bool cube_needs_rebuild_ { false };
  bool refresh_requested_ { false };

  std::function<void(oxygen::content::ResourceKey)> on_skybox_selected_;
};

} // namespace oxygen::examples::textured_cube::ui
