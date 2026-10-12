//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <random>
#include <string_view>

#include <glm/gtc/quaternion.hpp>
#include <glm/vec2.hpp>
#include <glm/vec3.hpp>

#include <Oxygen/Base/Macros.h>
#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Core/Constants.h>
#include <Oxygen/Core/EngineModule.h>
#include <Oxygen/Core/Types/View.h>
#include <Oxygen/Engine/AsyncEngine.h>
#include <Oxygen/Host/HostModule.h>
#include <Oxygen/OxCo/Co.h>
#include <Oxygen/Platform/Window.h>
#include <Oxygen/Scene/Scene.h>
#include <Oxygen/Scene/SceneNode.h>
#include <Oxygen/Vortex/CompositionView.h>
#include <Oxygen/Vortex/SceneRenderer/ShadingMode.h>
#include <Oxygen/Vortex/ShaderDebugMode.h>

namespace oxygen::engine {
class FrameContext;
} // namespace oxygen::engine

namespace oxygen::examples::vortex_basic {

class NormalMapValidationTexture;

struct ValidationOptions {
  enum class ExposureFixture : std::uint8_t {
    kNone,
    kFixed,
    kLockedAuto,
    kCurveCancellation,
  };
  bool sidedness_scene { false };
  ExposureFixture exposure_fixture { ExposureFixture::kNone };
  float fixed_exposure_ev { 14.0F };
  float exposure_key { 12.5F };
  float exposure_compensation { 0.0F };
  bool exposure_enabled { true };
  bool camera_exposure { false };
  bool inject_invalid_exposure { false };
  bool animate { false };
  bool normal_map { false };
  vortex::ShadingMode shading_mode { vortex::ShadingMode::kDeferred };
  [[nodiscard]] auto IsExposureFixture() const noexcept -> bool
  {
    return exposure_fixture != ExposureFixture::kNone;
  }
};

//! Environment features and tuning of the default validation scene.
struct SceneOptions {
  bool with_atmosphere { true };
  bool with_height_fog { true };
  bool with_local_fog { false };
  bool with_volumetric_fog { false };
  bool with_occlusion { false };
  bool with_translucency { false };
  bool local_fog_into_volumetric { true };
  bool volumetric_directional_shadows { true };
  bool volumetric_temporal_reprojection { true };
  float local_fog_volumetric_max_density { 0.01F };
  float local_fog_emissive_scale { 1.0F };
  float volumetric_fog_emissive_scale { 1.0F };
  float sky_light_volumetric_scattering_intensity { 1.0F };
  float aerial_scattering_strength { 1.0F };
  float aerial_start_depth_m { 100.0F };
};

//! Procedural native validation scenes for the Vortex renderer.
/*!
 Builds procedural geometry and a perspective camera, stages the scene for
 the host and renders it through one main view. The host owns the window,
 the view's framebuffers and the composition onto the window.
*/
class MainModule final : public host::HostModule {
  OXYGEN_TYPED(MainModule)

public:
  explicit MainModule(const host::HostContext& host,
    vortex::ShaderDebugMode shader_debug_mode
    = vortex::ShaderDebugMode::kDisabled,
    ValidationOptions validation = {}, SceneOptions scene_options = {});
  ~MainModule() override;

  OXYGEN_MAKE_NON_COPYABLE(MainModule)
  OXYGEN_MAKE_NON_MOVABLE(MainModule)

  [[nodiscard]] auto GetName() const noexcept -> std::string_view override
  {
    return "VortexBasicMainModule";
  }

  [[nodiscard]] auto GetPriority() const noexcept
    -> engine::ModulePriority override
  {
    constexpr auto kPriority = engine::ModulePriority { 500 };
    return kPriority;
  }

  [[nodiscard]] auto GetSupportedPhases() const noexcept
    -> engine::ModulePhaseMask override
  {
    return kHostPhases;
  }

  [[nodiscard]] auto IsCritical() const noexcept -> bool override
  {
    return true;
  }

  auto OnShutdown() noexcept -> void override;
  auto OnSceneMutation(observer_ptr<engine::FrameContext> context)
    -> co::Co<> override;

protected:
  auto OnHostAttached(observer_ptr<IAsyncEngine> engine) -> bool override;
  [[nodiscard]] auto BuildWindowProperties() const
    -> platform::window::Properties override;
  auto BuildViews(engine::FrameContext& context, host::HostViews& views)
    -> void override;

private:
  auto CreateScene(const char* name, std::size_t capacity) -> void;
  auto EnsureScene() -> void;
  auto BuildSidednessScene() -> void;
  auto BuildExposureScene() -> void;
  auto EnsureCamera(uint32_t width, uint32_t height) -> void;
  auto EnsureLighting() -> void;
  auto UpdateValidationScene(observer_ptr<engine::FrameContext> context)
    -> void;
  [[nodiscard]] auto ResolveViewExtent() const noexcept -> glm::uvec2;

  ValidationOptions validation_ {};
  SceneOptions scene_options_ {};
  std::unique_ptr<NormalMapValidationTexture> validation_normal_map_;
  scene::SceneNode validation_root_ {};
  IAsyncEngine::ModuleSubscription renderer_subscription_ {};

  std::shared_ptr<scene::Scene> scene_;
  scene::SceneNode cube_node_ {};
  scene::SceneNode occlusion_probe_node_ {};
  scene::SceneNode translucent_sphere_node_ {};
  scene::SceneNode translucent_cylinder_node_ {};
  scene::SceneNode floor_node_ {};
  scene::SceneNode camera_node_ {};
  scene::SceneNode directional_light_node_ {};
  scene::SceneNode point_light_node_ {};
  scene::SceneNode spot_light_node_ {};
  scene::SceneNode local_fog_volume_node_ {};

  ViewId main_view_id_ { kInvalidViewId };
  vortex::CompositionView::ViewStateHandle exposure_view_state_ {
    vortex::CompositionView::kInvalidViewStateHandle
  };
  static inline std::atomic<uint64_t> s_next_exposure_state_ { 1U };

  float animation_time_seconds_ { 0.0F };
  Quat cube_rotation_ { 1.0F, 0.0F, 0.0F, 0.0F };
  glm::vec3 cube_rotation_axis_ { 0.0F, 0.0F, 1.0F };
  std::mt19937 cube_rotation_rng_ { std::random_device {}() };
  vortex::ShaderDebugMode shader_debug_mode_ {
    vortex::ShaderDebugMode::kDisabled
  };
};

} // namespace oxygen::examples::vortex_basic
