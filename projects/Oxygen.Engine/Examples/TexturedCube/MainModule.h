//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include <glm/glm.hpp>

#include <Oxygen/Base/Macros.h>
#include <Oxygen/Content/ResourceKey.h>
#include <Oxygen/Core/EngineModule.h>
#include <Oxygen/Core/PhaseRegistry.h>
#include <Oxygen/Input/InputSystem.h>
#include <Oxygen/Scene/Scene.h>

#include "../Common/AsyncEngineApp.h"
#include "../Common/SingleViewExample.h"

namespace oxygen::examples::textured_cube {

class MainModule final : public common::SingleViewExample {
  OXYGEN_TYPED(MainModule)

public:
  using Base = oxygen::examples::common::SingleViewExample;

  enum class TextureIndexMode : std::uint8_t {
    kFallback = 0,
    kForcedError = 1,
    kCustom = 2,
  };

  enum class ImageOrigin : std::uint8_t {
    kTopLeft = 0,
    kBottomLeft = 1,
  };

  enum class UvOrigin : std::uint8_t {
    kBottomLeft = 0,
    kTopLeft = 1,
  };

  enum class OrientationFixMode : std::uint8_t {
    kNormalizeTextureOnUpload = 0,
    kNormalizeUvInTransform = 1,
    kNone = 2,
  };

  explicit MainModule(const oxygen::examples::common::AsyncEngineApp& app);

  [[nodiscard]] auto GetName() const noexcept -> std::string_view override
  {
    return "MainModule";
  }

  [[nodiscard]] auto GetPriority() const noexcept
    -> oxygen::engine::ModulePriority override
  {
    return engine::ModulePriority { 500 };
  }

  [[nodiscard]] auto GetSupportedPhases() const noexcept
    -> oxygen::engine::ModulePhaseMask override
  {
    using namespace core;
    return engine::MakeModuleMask<PhaseId::kFrameStart, PhaseId::kSceneMutation,
      PhaseId::kGameplay, PhaseId::kGuiUpdate, PhaseId::kPreRender,
      PhaseId::kCompositing, PhaseId::kFrameEnd>();
  }

  ~MainModule() override = default;

  OXYGEN_MAKE_NON_COPYABLE(MainModule);
  OXYGEN_MAKE_NON_MOVABLE(MainModule);

  auto OnAttached(oxygen::observer_ptr<oxygen::AsyncEngine> engine) noexcept
    -> bool override;
  auto OnShutdown() noexcept -> void override;

  auto OnFrameStart(oxygen::engine::FrameContext& context) -> void override;
  auto OnExampleFrameStart(engine::FrameContext& context) -> void override;
  auto OnSceneMutation(engine::FrameContext& context) -> co::Co<> override;
  auto OnGameplay(engine::FrameContext& context) -> co::Co<> override;
  auto OnGuiUpdate(engine::FrameContext& context) -> co::Co<> override;
  auto OnPreRender(engine::FrameContext& context) -> co::Co<> override;
  auto OnCompositing(engine::FrameContext& context) -> co::Co<> override;
  auto OnFrameEnd(engine::FrameContext& context) -> void override;

protected:
  auto BuildDefaultWindowProperties() const
    -> platform::window::Properties override;

private:
  auto InitInputBindings() noexcept -> bool;
  auto EnsureMainCamera(int width, int height) -> void;
  auto ApplyOrbitAndZoom() -> void;
  auto DrawDebugOverlay(engine::FrameContext& context) -> void;

  [[nodiscard]] auto GetEffectiveUvTransform() const
    -> std::pair<glm::vec2, glm::vec2>;

  std::shared_ptr<scene::Scene> scene_;
  scene::SceneNode main_camera_;
  scene::SceneNode cube_node_;
  scene::SceneNode sun_node_;
  scene::SceneNode fill_light_node_;

  std::shared_ptr<oxygen::input::Action> zoom_in_action_;
  std::shared_ptr<oxygen::input::Action> zoom_out_action_;
  std::shared_ptr<oxygen::input::Action> rmb_action_;
  std::shared_ptr<oxygen::input::Action> orbit_action_;

  std::shared_ptr<oxygen::input::InputMappingContext> camera_controls_ctx_;

  TextureIndexMode texture_index_mode_ { TextureIndexMode::kForcedError };
  std::uint32_t custom_texture_resource_index_ { 0U };
  oxygen::content::ResourceKey custom_texture_key_ { 0U };
  oxygen::content::ResourceKey forced_error_key_ { 0U };
  glm::vec2 uv_scale_ { 1.0f, 1.0f };
  glm::vec2 uv_offset_ { 0.0f, 0.0f };
  bool cube_needs_rebuild_ { true };

  // Texture/UV origin normalization controls (demo-only).
  UvOrigin uv_origin_ { UvOrigin::kBottomLeft };
  ImageOrigin image_origin_ { ImageOrigin::kTopLeft };
  OrientationFixMode orientation_fix_mode_ {
    OrientationFixMode::kNormalizeTextureOnUpload
  };
  bool extra_flip_u_ { false };
  bool extra_flip_v_ { false };

  std::shared_ptr<const oxygen::data::MaterialAsset> cube_material_;
  std::shared_ptr<oxygen::data::GeometryAsset> cube_geometry_;
  std::vector<std::shared_ptr<oxygen::data::GeometryAsset>>
    retired_cube_geometries_;

  std::array<char, 512> img_path_ {};
  bool img_load_requested_ { false };
  std::string img_status_message_ {};
  int img_last_width_ { 0 };
  int img_last_height_ { 0 };

  // Importer test options
  int img_output_format_idx_ { 0 }; // 0=RGBA8, 1=BC7, 2=RGBA16F, 3=RGBA32F
  bool generate_mips_ { true };
  bool tonemap_hdr_to_ldr_ { false }; // Default OFF - let user choose
  float hdr_exposure_ev_ { 0.0F };

  //=== Skybox Settings ===---------------------------------------------------//

  //! Layout of the input skybox image.
  enum class SkyboxLayout : int {
    kEquirectangular = 0, //!< 2:1 panorama
    kHorizontalCross = 1, //!< 4x3 cross layout
    kVerticalCross = 2, //!< 3x4 cross layout
    kHorizontalStrip = 3, //!< 6x1 strip
    kVerticalStrip = 4, //!< 1x6 strip
  };

  //! Output format for the skybox cubemap.
  enum class SkyboxOutputFormat : int {
    kRGBA8 = 0, //!< LDR 8-bit
    kRGBA16Float = 1, //!< HDR 16-bit float
    kRGBA32Float = 2, //!< HDR 32-bit float
    kBC7 = 3, //!< BC7 compressed (LDR)
  };

  std::array<char, 512> skybox_path_ {};
  bool skybox_load_requested_ { false };
  bool skybox_reupload_requested_ { false };
  std::string skybox_status_message_ {};
  int skybox_last_face_size_ { 0 };
  oxygen::content::ResourceKey skybox_texture_key_ { 0U };
  SkyboxLayout skybox_layout_ { SkyboxLayout::kEquirectangular };
  SkyboxOutputFormat skybox_output_format_ { SkyboxOutputFormat::kRGBA8 };
  int skybox_cube_face_size_ { 512 };
  bool skybox_flip_y_ { false }; //!< Flip Y for standard equirectangular HDRIs

  float sky_light_intensity_ { 1.0f };
  float sky_light_diffuse_intensity_ { 1.0f };
  float sky_light_specular_intensity_ { 1.0f };

  float sun_intensity_ { 12.0f };
  glm::vec3 sun_color_rgb_ { 1.0f, 0.98f, 0.95f };

  std::vector<std::byte> skybox_rgba8_ {};
  std::uint32_t skybox_width_ { 0U };
  std::uint32_t skybox_height_ { 0U };

  bool sun_ray_dir_from_skybox_ { false };
  glm::vec3 sun_ray_dir_ws_ { 0.35f, -0.45f, -1.0f };

  glm::vec3 camera_target_ { 0.0f, 0.0f, 0.0f };
  float orbit_yaw_rad_ { -glm::half_pi<float>() };
  float orbit_pitch_rad_ { 0.0f };
  float orbit_distance_ { 6.0f };
  float orbit_sensitivity_ { 0.01f };
  float zoom_step_ { 0.75f };
  float min_cam_distance_ { 1.25f };
  float max_cam_distance_ { 40.0f };
};

} // namespace oxygen::examples::textured_cube
