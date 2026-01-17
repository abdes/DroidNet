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

#include <glm/glm.hpp>

#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Core/FrameContext.h>
#include <Oxygen/Core/Types/Format.h>
#include <Oxygen/Core/Types/TextureType.h>

#include "CameraController.h"
#include "SceneSetup.h"

// Forward declarations
struct ImGuiContext;

namespace oxygen::engine {
class Renderer;
struct ShaderPassConfig;
} // namespace oxygen::engine

namespace oxygen::data {
class MaterialAsset;
} // namespace oxygen::data

namespace oxygen::examples::textured_cube {

//! Debug UI for the TexturedCube demo.
/*!
 This class manages the ImGui-based debug overlay with controls for:
 - Texture selection and loading
 - UV transformation
 - Lighting parameters
 - Skybox configuration

 ### Usage

 ```cpp
 DebugUI ui;
 ui.Draw(context, camera, scene_setup, skybox_manager, renderer);
 ```
*/
class DebugUI final {
public:
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

  //! State for the cooked texture import UI.
  struct ImportState {
    std::array<char, 512> cooked_root {};
    std::array<char, 512> source_path {};
    int import_kind { 0 };
    int output_format_idx { 0 };
    bool generate_mips { true };
    int max_mip_levels { 0 };
    int mip_filter_idx { 1 };
    bool flip_y { false };
    bool force_rgba { true };
    int cube_face_size { 512 };
    int layout_idx { 0 };
    bool import_requested { false };
    bool refresh_requested { false };
    bool import_in_flight { false };
    float import_progress { 0.0f };
    std::string status_message {};
  };

  //! State for per-object texture selection.
  struct TextureSlotState {
    SceneSetup::TextureIndexMode mode {
      SceneSetup::TextureIndexMode::kFallback
    };
    std::uint32_t resource_index { 0U };
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
    oxygen::Format format { oxygen::Format::kUnknown };
    oxygen::TextureType texture_type { oxygen::TextureType::kTexture2D };
  };

  //! Action emitted by the cooked texture browser.
  struct BrowserAction {
    enum class Type : std::uint8_t {
      kNone = 0,
      kSetSphere = 1,
      kSetCube = 2,
      kSetSkybox = 3,
    };

    Type type { Type::kNone };
    std::uint32_t entry_index { 0U };
  };

  //! State for the demo surface material.
  struct SurfaceState {
    // Default: reflective metal (not pure chrome).
    float metalness { 0.85f };
    float roughness { 0.12f };

    // When enabled, the material skips texture sampling and uses a constant
    // base color. This is useful to isolate PBR+IBL behavior.
    bool use_constant_base_color { false };
    glm::vec3 constant_base_color_rgb { 0.82f, 0.82f, 0.82f };
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

  //! State for lighting UI.
  struct LightingState {
    float sky_light_intensity { 1.0f };
    float sky_light_diffuse { 1.0f };
    float sky_light_specular { 1.0f };
    float sun_intensity { 12.0f };
    glm::vec3 sun_color_rgb { 1.0f, 0.98f, 0.95f };
  };

  DebugUI() = default;
  ~DebugUI() = default;

  DebugUI(const DebugUI&) = delete;
  auto operator=(const DebugUI&) -> DebugUI& = delete;
  DebugUI(DebugUI&&) = delete;
  auto operator=(DebugUI&&) -> DebugUI& = delete;

  //! Draw the debug overlay.
  auto Draw(engine::FrameContext& context, const CameraController& camera,
    oxygen::observer_ptr<oxygen::engine::Renderer> renderer,
    oxygen::engine::ShaderPassConfig* shader_pass_config,
    const std::shared_ptr<const oxygen::data::MaterialAsset>& sphere_material,
    const std::shared_ptr<const oxygen::data::MaterialAsset>& cube_material,
    bool& cube_needs_rebuild) -> void;

  //! Check if an import was requested.
  [[nodiscard]] auto IsImportRequested() const -> bool
  {
    return import_state_.import_requested;
  }

  //! Clear import request flag.
  auto ClearImportRequest() -> void { import_state_.import_requested = false; }

  //! Check if a cooked-root refresh was requested.
  [[nodiscard]] auto IsRefreshRequested() const -> bool
  {
    return import_state_.refresh_requested;
  }

  //! Clear refresh request flag.
  auto ClearRefreshRequest() -> void
  {
    import_state_.refresh_requested = false;
  }

  //! Get import UI state.
  auto GetImportState() -> ImportState& { return import_state_; }

  //! Set import status message and progress.
  auto SetImportStatus(const std::string& message, const bool in_flight,
    const float progress) -> void
  {
    import_state_.status_message = message;
    import_state_.import_in_flight = in_flight;
    import_state_.import_progress = progress;
  }

  //! Set the cooked texture entries for browsing.
  auto SetCookedTextureEntries(std::vector<CookedTextureEntry> entries) -> void
  {
    cooked_entries_ = std::move(entries);
  }

  //! Consume the next browser action, if any.
  auto ConsumeBrowserAction(BrowserAction& action) -> bool
  {
    if (browser_action_.type == BrowserAction::Type::kNone) {
      return false;
    }
    action = browser_action_;
    browser_action_.type = BrowserAction::Type::kNone;
    browser_action_.entry_index = 0U;
    return true;
  }

  //! Get the sphere texture state.
  auto GetSphereTextureState() -> TextureSlotState& { return sphere_texture_; }

  //! Get the cube texture state.
  auto GetCubeTextureState() -> TextureSlotState& { return cube_texture_; }

  //! Get UV state for reading/writing.
  auto GetUvState() -> UvState& { return uv_state_; }

  //! Get surface state for reading/writing.
  auto GetSurfaceState() -> SurfaceState& { return surface_state_; }

  //! Get lighting state for reading/writing.
  auto GetLightingState() -> LightingState& { return lighting_state_; }

  //! Get the effective UV transform (after applying orientation fixes).
  [[nodiscard]] auto GetEffectiveUvTransform() const
    -> std::pair<glm::vec2, glm::vec2>;

private:
  auto DrawMaterialsTab(oxygen::observer_ptr<oxygen::engine::Renderer> renderer,
    const std::shared_ptr<const oxygen::data::MaterialAsset>& sphere_material,
    const std::shared_ptr<const oxygen::data::MaterialAsset>& cube_material,
    bool& cube_needs_rebuild) -> void;

  auto DrawImportWindow() -> void;

  auto DrawCookedBrowserWindow() -> void;

  auto DrawLightingTab(oxygen::observer_ptr<scene::Scene> scene,
    oxygen::observer_ptr<oxygen::engine::Renderer> renderer,
    oxygen::engine::ShaderPassConfig* shader_pass_config) -> void;

  ImportState import_state_;
  TextureSlotState sphere_texture_ {};
  TextureSlotState cube_texture_ {};
  std::vector<CookedTextureEntry> cooked_entries_ {};
  BrowserAction browser_action_ {};
  SurfaceState surface_state_;
  UvState uv_state_;
  LightingState lighting_state_;
};

} // namespace oxygen::examples::textured_cube
