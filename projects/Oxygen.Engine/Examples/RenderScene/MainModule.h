//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstdint>
#include <deque>
#include <filesystem>
#include <memory>
#include <optional>
#include <unordered_map>
#include <vector>

#include <glm/vec3.hpp>

#include <Oxygen/Base/Macros.h>
#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Core/EngineModule.h>
#include <Oxygen/Core/PhaseRegistry.h>
#include <Oxygen/Data/AssetKey.h>
#include <Oxygen/Platform/Window.h>
#include <Oxygen/Scene/SceneNode.h>

#include "DemoShell/DemoShell.h"
#include "DemoShell/Runtime/DemoAppContext.h"
#include "DemoShell/Runtime/DemoModuleBase.h"
#include "DemoShell/UI/ContentVm.h"

namespace oxygen {
class IAsyncEngine;
namespace engine {
  class FrameContext;
}
namespace data {
  class SceneAsset;
  class PhysicsSceneAsset;
}
namespace vortex {
  struct CompositionView;
}
} // namespace oxygen

namespace oxygen::examples {
class SceneLoaderService;
class SkyboxService;
} // namespace oxygen::examples

namespace oxygen::examples::render_scene {

class MainModule final : public DemoModuleBase {
  OXYGEN_TYPED(MainModule)
public:
  using Base = oxygen::examples::DemoModuleBase;

  explicit MainModule(const oxygen::examples::DemoAppContext& app);

  [[nodiscard]] auto GetName() const noexcept -> std::string_view override
  {
    return "MainModule";
  }

  [[nodiscard]] auto GetPriority() const noexcept
    -> oxygen::engine::ModulePriority override
  {
    constexpr oxygen::engine::ModulePriority kPriority { 500 };
    return kPriority;
  }

  [[nodiscard]] auto GetSupportedPhases() const noexcept
    -> oxygen::engine::ModulePhaseMask override
  {
    using enum core::PhaseId;
    return engine::MakeModuleMask<kFrameStart, kInput, kSceneMutation,
      kGameplay, kPublishViews, kGuiUpdate, kPreRender, kCompositing,
      kFrameEnd>();
  }

  ~MainModule() override;

  OXYGEN_MAKE_NON_COPYABLE(MainModule);
  OXYGEN_MAKE_NON_MOVABLE(MainModule);

  auto BuildDefaultWindowProperties() const
    -> platform::window::Properties override;

  auto ClearBackbufferReferences() -> void override;
  auto UpdateComposition(engine::FrameContext& context,
    std::vector<vortex::CompositionView>& views) -> void override;

  auto OnAttachedImpl(
    oxygen::observer_ptr<oxygen::IAsyncEngine> engine) noexcept
    -> std::unique_ptr<DemoShell> override;
  void OnShutdown() noexcept override;

  auto OnFrameStart(observer_ptr<oxygen::engine::FrameContext> context)
    -> void override;
  auto OnInput(observer_ptr<engine::FrameContext> context) -> co::Co<> override;
  auto OnSceneMutation(observer_ptr<engine::FrameContext> context)
    -> co::Co<> override;
  auto OnGameplay(observer_ptr<engine::FrameContext> context)
    -> co::Co<> override;
  auto OnGuiUpdate(observer_ptr<engine::FrameContext> context)
    -> co::Co<> override;
  auto OnPreRender(observer_ptr<engine::FrameContext> context)
    -> co::Co<> override;

private:
  auto ReleaseCurrentSceneAsset(const char* reason) -> void;

  auto ClearSceneRuntime(const char* reason) -> void;
  auto StageFallbackScene() -> void;
  auto ApplyStartupSkyboxToScene(
    oxygen::observer_ptr<oxygen::scene::Scene> scene,
    std::string_view scene_label) -> void;
  auto ApplySkyLightLifecycleProofToggle(oxygen::scene::Scene& scene,
    std::uint64_t frame_index) -> void;

  struct SceneLoadRequest {
    data::AssetKey key {};
    ui::SceneSourceKind source_kind { ui::SceneSourceKind::kPak };
    std::filesystem::path source_path;
    std::string scene_name;
  };

  // Scene and rendering.
  ActiveScene active_scene_;
  ViewId main_view_id_ { kInvalidViewId };
  scene::SceneNode main_camera_;

  std::shared_ptr<oxygen::examples::SceneLoaderService> scene_loader_;
  bool scene_load_cancel_requested_ { false };
  std::shared_ptr<data::PhysicsSceneAsset> pending_physics_sidecar_;
  bool scene_published_this_frame_ { false };

  // Content and scene state
  std::optional<data::AssetKey> current_scene_key_;
  std::shared_ptr<data::SceneAsset> active_scene_asset_pin_;
  std::optional<data::AssetKey> last_released_scene_key_;
  std::optional<data::AssetKey> active_scene_load_key_;
  Extent<uint32_t> last_viewport_;

  // Debug/instrumentation.
  bool logged_gameplay_tick_ { false };
  bool was_orbiting_last_frame_ { false };

  // Deferred lifecycle actions (applied in OnFrameStart)
  enum class PendingSourceAction : uint8_t {
    kNone,
    kClear,
    kTrimCache,
    kMountPak,
    kMountIndex,
  };
  struct PendingSourceRequest final {
    PendingSourceAction action { PendingSourceAction::kNone };
    std::filesystem::path path;
  };
  std::deque<PendingSourceRequest> pending_source_requests_;
  std::optional<SceneLoadRequest> pending_scene_load_;
  std::optional<std::string> startup_scene_name_;
  std::optional<std::filesystem::path> startup_skybox_path_;
  int startup_skybox_layout_ { 0 };
  int startup_skybox_output_format_ { 0 };
  int startup_skybox_face_size_ { 512 };
  bool startup_skybox_flip_y_ { false };
  bool startup_skybox_tonemap_hdr_to_ldr_ { false };
  float startup_skybox_hdr_exposure_ev_ { 0.0F };
  float startup_sky_sphere_intensity_ { 1.0F };
  float startup_sky_light_intensity_mul_ { 1.0F };
  float startup_sky_light_diffuse_ { 1.0F };
  float startup_sky_light_specular_ { 1.0F };
  bool startup_sky_light_real_time_capture_enabled_ { false };
  glm::vec3 startup_sky_light_tint_ { 1.0F, 1.0F, 1.0F };
  bool startup_sky_light_lifecycle_proof_enabled_ { false };
  std::uint32_t startup_sky_light_lifecycle_disable_frame_ { 0U };
  std::uint32_t startup_sky_light_lifecycle_enable_frame_ { 0U };
  bool startup_sky_light_lifecycle_disable_applied_ { false };
  bool startup_sky_light_lifecycle_enable_applied_ { false };
  std::unique_ptr<SkyboxService> startup_skybox_service_;
  bool startup_scene_load_requested_ { false };
  bool startup_scene_missing_logged_ { false };
  std::unordered_map<std::filesystem::path, std::filesystem::file_time_type>
    mounted_pak_write_times_;
  std::unordered_map<std::filesystem::path, std::filesystem::file_time_type>
    mounted_loose_index_write_times_;
  bool pending_scene_clear_ { false };
};

} // namespace oxygen::examples::render_scene
