//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <array>
#include <cstdint>
#include <string>

#include <glm/glm.hpp>

#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Core/FrameContext.h>

#include "CameraController.h"
#include "SceneSetup.h"
#include "SkyboxManager.h"

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

  //! State for texture loading UI.
  struct TextureState {
    std::array<char, 512> path {};
    bool load_requested { false };
    std::string status_message {};
    int last_width { 0 };
    int last_height { 0 };
    int output_format_idx { 0 };
    bool generate_mips { true };
    bool tonemap_hdr_to_ldr { false };
    float hdr_exposure_ev { 0.0f };
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

  //! State for skybox UI.
  struct SkyboxState {
    std::array<char, 512> path {};
    bool load_requested { false };
    std::string status_message {};
    int last_face_size { 0 };
    int layout_idx { 0 };
    int output_format_idx { 0 };
    int cube_face_size { 512 };
    bool flip_y { false };
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
    SceneSetup::TextureIndexMode& texture_mode,
    std::uint32_t& custom_texture_resource_index,
    oxygen::observer_ptr<oxygen::engine::Renderer> renderer,
    oxygen::engine::ShaderPassConfig* shader_pass_config,
    const std::shared_ptr<const oxygen::data::MaterialAsset>& cube_material,
    bool& cube_needs_rebuild) -> void;

  //! Check if texture load was requested.
  [[nodiscard]] auto IsTextureLoadRequested() const -> bool
  {
    return texture_state_.load_requested;
  }

  //! Clear texture load request flag.
  auto ClearTextureLoadRequest() -> void
  {
    texture_state_.load_requested = false;
  }

  //! Check if skybox load was requested.
  [[nodiscard]] auto IsSkyboxLoadRequested() const -> bool
  {
    return skybox_state_.load_requested;
  }

  //! Clear skybox load request flag.
  auto ClearSkyboxLoadRequest() -> void
  {
    skybox_state_.load_requested = false;
  }

  //! Get texture state for reading/writing.
  auto GetTextureState() -> TextureState& { return texture_state_; }

  //! Get skybox state for reading/writing.
  auto GetSkyboxState() -> SkyboxState& { return skybox_state_; }

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
  auto DrawMaterialsTab(SceneSetup::TextureIndexMode& texture_mode,
    std::uint32_t& custom_texture_resource_index,
    oxygen::observer_ptr<oxygen::engine::Renderer> renderer,
    const std::shared_ptr<const oxygen::data::MaterialAsset>& cube_material,
    bool& cube_needs_rebuild) -> void;

  auto DrawLightingTab(oxygen::observer_ptr<scene::Scene> scene,
    oxygen::observer_ptr<oxygen::engine::Renderer> renderer,
    oxygen::engine::ShaderPassConfig* shader_pass_config) -> void;

  TextureState texture_state_;
  SurfaceState surface_state_;
  SkyboxState skybox_state_;
  UvState uv_state_;
  LightingState lighting_state_;
};

} // namespace oxygen::examples::textured_cube
