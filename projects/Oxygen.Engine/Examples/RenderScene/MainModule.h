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

#include "DemoShell/DemoShell.h"
#include "DemoShell/Runtime/DemoModuleBase.h"
#include "DemoShell/UI/ContentVm.h"
#include <glm/vec3.hpp>

#include <Oxygen/Base/Macros.h>
#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Core/EngineModule.h>
#include <Oxygen/Core/PhaseRegistry.h>
#include <Oxygen/Core/Types/AssetKey.h>
#include <Oxygen/Host/HostConfig.h>
#include <Oxygen/Host/SceneStage.h>
#include <Oxygen/Platform/Window.h>
#include <Oxygen/Scene/SceneNode.h>
#include <Oxygen/Vortex/CompositionView.h>

namespace oxygen {
class IAsyncEngine;
namespace engine {
  class FrameContext;
}
namespace data {
  class SceneAsset;
  class PhysicsSceneAsset;
}
} // namespace oxygen

namespace oxygen::examples {
class SceneLoaderService;
class SkyboxService;
} // namespace oxygen::examples

namespace oxygen::examples::render_scene {

//! What RenderScene loads or proves at startup, from its command line.
struct StartupOptions {
  //! A cooked scene to load: name, virtual path, stem or key.
  std::string scene_name;
  //! A cubemap the startup scene's environment uses.
  std::string skybox_path;
  bool preview_sun_enabled { false };
  std::optional<int> environment_profile;
  //! Disables then re-enables the static sky light at these frames.
  bool sky_light_lifecycle_proof_enabled { false };
  std::uint32_t sky_light_lifecycle_disable_frame { 0U };
  std::uint32_t sky_light_lifecycle_enable_frame { 0U };
};

class MainModule final : public DemoModuleBase {
  OXYGEN_TYPED(MainModule)
public:
  using Base = oxygen::examples::DemoModuleBase;

  MainModule(const host::HostContext& host, StartupOptions options);

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
    return kDemoPhases;
  }

  ~MainModule() override;

  OXYGEN_MAKE_NON_COPYABLE(MainModule);
  OXYGEN_MAKE_NON_MOVABLE(MainModule);

  auto OnAttachedImpl(
    oxygen::observer_ptr<oxygen::IAsyncEngine> engine) noexcept
    -> std::unique_ptr<DemoShell> override;
  void OnShutdown() noexcept override;

  auto OnFrameStart(observer_ptr<oxygen::engine::FrameContext> context)
    -> void override;
  auto OnSceneMutation(observer_ptr<engine::FrameContext> context)
    -> co::Co<> override;

protected:
  [[nodiscard]] auto BuildWindowProperties() const
    -> platform::window::Properties override;
  auto OnScenePublished(engine::FrameContext& context) -> void override;
  auto BuildViews(engine::FrameContext& context, host::HostViews& views)
    -> void override;

private:
#ifdef OXYGEN_BUILD_UI_TESTS
  auto RegisterUiTests(ImGuiTestEngine* engine) -> void override;
#endif
  auto ReleaseCurrentSceneAsset(const char* reason) -> void;

  auto ResetMainViewState(observer_ptr<engine::FrameContext> context = nullptr)
    -> void;
  auto ClearSceneRuntime(const char* reason) -> void;
  auto StageFallbackScene() -> void;

  auto ApplySkyLightLifecycleProofToggle(
    oxygen::scene::Scene& scene, std::uint64_t frame_index) -> void;

  struct SceneLoadRequest {
    data::AssetKey key;
    ui::SceneSourceKind source_kind { ui::SceneSourceKind::kPak };
    std::filesystem::path source_path;
    std::string scene_name;
  };

  // Scene and rendering.
  host::ActiveScene active_scene_;
  ViewId main_view_id_ { kInvalidViewId };
  scene::SceneNode main_camera_;
  //! Temporal history belongs to this scene/camera session, not main_view_id_.
  vortex::CompositionView::ViewStateHandle main_view_state_handle_ {
    vortex::CompositionView::kInvalidViewStateHandle
  };
  host::ActiveScene main_view_state_scene_;
  scene::SceneNode main_view_state_camera_;

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

  // Deferred lifecycle actions (applied in OnFrameStart)
  enum class PendingSourceAction : uint8_t {
    kNone,
    kClear,
    kMountPak,
    kMountIndex,
    kMountGeneration,
  };
  struct PendingSourceRequest final {
    PendingSourceAction action { PendingSourceAction::kNone };
    std::filesystem::path path {};
    std::optional<std::filesystem::path> previous_root;
  };
  std::deque<PendingSourceRequest> pending_source_requests_;
  std::optional<SceneLoadRequest> pending_scene_load_;
  StartupOptions startup_;
  std::optional<std::string> startup_scene_name_;
  bool startup_sky_light_lifecycle_disable_applied_ { false };
  bool startup_sky_light_lifecycle_enable_applied_ { false };
  bool startup_scene_load_requested_ { false };
  bool startup_scene_missing_logged_ { false };
  std::unordered_map<std::filesystem::path, std::filesystem::file_time_type>
    mounted_pak_write_times_;
  std::unordered_map<std::filesystem::path, std::filesystem::file_time_type>
    mounted_loose_index_write_times_;
  bool pending_scene_clear_ { false };
  bool loaded_scene_active_ { false };
};

} // namespace oxygen::examples::render_scene
