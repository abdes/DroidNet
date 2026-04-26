//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <atomic>
#include <memory>
#include <optional>
#include <random>
#include <string_view>

#include <glm/gtc/quaternion.hpp>
#include <glm/vec2.hpp>
#include <glm/vec3.hpp>

#include <Oxygen/Base/Macros.h>
#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Composition/Composition.h>
#include <Oxygen/Core/EngineModule.h>
#include <Oxygen/Core/PhaseRegistry.h>
#include <Oxygen/Core/Types/ResolvedView.h>
#include <Oxygen/Engine/AsyncEngine.h>
#include <Oxygen/OxCo/Co.h>
#include <Oxygen/Platform/Types.h>
#include <Oxygen/Scene/Scene.h>
#include <Oxygen/Scene/SceneNode.h>
#include <Oxygen/Vortex/ShaderDebugMode.h>

namespace oxygen {
class Graphics;
namespace engine {
  class FrameContext;
}
namespace graphics {
  class Framebuffer;
  class Surface;
}
namespace vortex {
  class Renderer;
}
} // namespace oxygen

namespace oxygen::examples {
class DemoAppContext;
class AppWindow;
} // namespace oxygen::examples

namespace oxygen::examples::vortex_basic {

//! Minimal engine module that exercises the Vortex deferred renderer.
/*!
 Creates a single procedural cube, a perspective camera, and registers
 a scene view with the Vortex renderer each frame. Supports RenderDoc
 frame capture via the standard CLI flags.

 This module intentionally bypasses DemoModuleBase and DemoShell because
 those are coupled to the legacy ForwardPipeline renderer. Instead it
 owns an AppWindow directly and talks to vortex::Renderer.
*/
class MainModule final : public engine::EngineModule, public Composition {
  OXYGEN_TYPED(MainModule)

public:
  explicit MainModule(const DemoAppContext& app,
    vortex::ShaderDebugMode shader_debug_mode
    = vortex::ShaderDebugMode::kDisabled) noexcept;
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
    using enum core::PhaseId;
    return engine::MakeModuleMask<kFrameStart, kSceneMutation, kPublishViews,
      kCompositing, kFrameEnd>();
  }

  [[nodiscard]] auto IsCritical() const noexcept -> bool override
  {
    return true;
  }

  auto OnAttached(observer_ptr<IAsyncEngine> engine) noexcept -> bool override;
  auto OnShutdown() noexcept -> void override;

  auto OnFrameStart(observer_ptr<engine::FrameContext> context)
    -> void override;
  auto OnSceneMutation(observer_ptr<engine::FrameContext> context)
    -> co::Co<> override;
  auto OnPublishViews(observer_ptr<engine::FrameContext> context)
    -> co::Co<> override;
  auto OnCompositing(observer_ptr<engine::FrameContext> context)
    -> co::Co<> override;

private:
  auto ReleasePublishedRuntimeView(
    observer_ptr<engine::FrameContext> context = nullptr) -> void;
  auto ResolveVortexRenderer() -> observer_ptr<vortex::Renderer>;
  [[nodiscard]] auto BuildResolvedView(uint32_t width, uint32_t height)
    -> std::optional<ResolvedView>;
  auto EnsureScene() -> void;
  auto EnsureCamera(uint32_t width, uint32_t height) -> void;
  auto EnsureLighting() -> void;
  auto UpdateValidationScene(observer_ptr<engine::FrameContext> context)
    -> void;
  [[nodiscard]] auto ResolveViewExtent() const noexcept -> glm::uvec2;
  auto EnsureSceneFb(uint32_t width, uint32_t height) -> void;
  auto ClearSceneFb() -> void;

  const DemoAppContext& app_;
  observer_ptr<AppWindow> app_window_ { nullptr };
  observer_ptr<graphics::Surface> last_surface_ { nullptr };
  observer_ptr<vortex::Renderer> vortex_renderer_ { nullptr };
  IAsyncEngine::ModuleSubscription renderer_subscription_ {};

  std::shared_ptr<scene::Scene> scene_;
  scene::SceneNode cube_node_ {};
  scene::SceneNode occlusion_probe_node_ {};
  scene::SceneNode floor_node_ {};
  scene::SceneNode camera_node_ {};
  scene::SceneNode directional_light_node_ {};
  scene::SceneNode point_light_node_ {};
  scene::SceneNode spot_light_node_ {};
  scene::SceneNode local_fog_volume_node_ {};

  ViewId main_view_id_ { kInvalidViewId };
  static inline std::atomic<uint64_t> s_next_view_id_ { 2000 };

  // Intermediate scene framebuffer (color-only, matching window size).
  std::shared_ptr<graphics::Framebuffer> scene_fb_;
  uint32_t scene_fb_width_ { 0 };
  uint32_t scene_fb_height_ { 0 };
  float animation_time_seconds_ { 0.0F };
  Quat cube_rotation_ { 1.0F, 0.0F, 0.0F, 0.0F };
  glm::vec3 cube_rotation_axis_ { 0.0F, 0.0F, 1.0F };
  std::mt19937 cube_rotation_rng_ { std::random_device {}() };
  vortex::ShaderDebugMode shader_debug_mode_ {
    vortex::ShaderDebugMode::kDisabled
  };
};

} // namespace oxygen::examples::vortex_basic
