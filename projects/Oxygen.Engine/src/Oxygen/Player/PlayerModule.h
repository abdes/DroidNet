//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Commands/Command.h>
#include <Oxygen/Composition/Typed.h>
#include <Oxygen/Core/Types/View.h>
#include <Oxygen/Host/HostModule.h>
#include <Oxygen/Player/LaunchFile.h>
#include <Oxygen/Player/PlayerProtocol.h>
#include <Oxygen/Scene/SceneNode.h>
#include <Oxygen/Scripting/Module/ScriptingModule.h>
#include <Oxygen/Vortex/CompositionView.h>
#include <Oxygen/Vortex/Diagnostics/DiagnosticsService.h>

namespace oxygen::scene {
class Scene;
} // namespace oxygen::scene

namespace oxygen::control {
class EventLog;
} // namespace oxygen::control

namespace oxygen::player {

struct SessionLoad;
struct Remount;
struct PlayOutcome;

//! Runs the launch file's scene as play sessions.
/*!
 Mounts the launch mounts in order at attach and begins a session at once.
 A session loads the scene by virtual path and instantiates it with scripts
 and starting input contexts in a task the module cancels when the session
 ends; the finished scene is staged and published at the next frame start.
 The one main view renders through the scene's active camera every frame,
 so a script that moves or switches the camera moves the view.

 Without a scene (stopped, loading, failed) the window shows why. Play
 (`ngin.session.begin`, from the toolbar, the console or a remote client)
 reads the launch file again and remounts the cooked roots and pak files it
 names, so content a launcher published since is seen, and replies with the
 load outcome. Lifecycle, script diagnostic and GPU work events go
 to the host's event log.
*/
class PlayerModule final : public host::HostModule {
  OXYGEN_TYPED(PlayerModule)

public:
  //! `host` must outlive the module. `launch` was read from `launch_path`,
  //! which every Play reads again. `startup_cvars` names the console
  //! variables the command line set; unknown names are reported once every
  //! module is attached.
  PlayerModule(const host::HostContext& host, PlayerLaunch launch,
    std::filesystem::path launch_path,
    std::vector<std::string> startup_cvars = {});
  ~PlayerModule() override;

  OXYGEN_MAKE_NON_COPYABLE(PlayerModule)
  OXYGEN_MAKE_NON_MOVABLE(PlayerModule)

  [[nodiscard]] auto GetName() const noexcept -> std::string_view override
  {
    return "PlayerModule";
  }
  [[nodiscard]] auto GetPriority() const noexcept
    -> engine::ModulePriority override
  {
    return engine::ModulePriority { kPriority };
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
  auto OnSessionBegin(observer_ptr<IAsyncEngine> engine) noexcept
    -> bool override;
  auto OnSessionEnd(observer_ptr<IAsyncEngine> engine) noexcept
    -> void override;
  auto RegisterCommands(
    observer_ptr<commands::CommandRegistry> registry) noexcept -> void override;

  auto OnFrameStart(observer_ptr<engine::FrameContext> context)
    -> void override;
  auto OnGuiUpdate(observer_ptr<engine::FrameContext> context)
    -> co::Co<> override;

protected:
  auto OnHostAttached(observer_ptr<IAsyncEngine> engine) -> bool override;
  [[nodiscard]] auto BuildWindowProperties() const
    -> platform::window::Properties override;
  auto BuildViews(engine::FrameContext& context, host::HostViews& views)
    -> void override;
  [[nodiscard]] auto RetainInactiveView(ViewId view_id) const noexcept
    -> bool override;

private:
  //! Before the renderer, after input and scripting.
  static constexpr std::uint32_t kPriority = 500U;

  auto HandlePlay(const std::optional<std::string>& scene)
    -> commands::CommandResult;
  //! Replies to every Play waiting for the session's load outcome.
  auto ResolvePlay(const commands::CommandResult& result) -> void;
  auto Publish(LifecycleState state, std::string_view message = {},
    std::span<const instantiation::SceneInstantiationIssue> issues = {},
    std::string_view code = {}) -> void;
  auto UpdateGpuWorkLease() -> void;
  auto StartRemount() -> void;
  auto FinishRemount() -> void;
  auto FinishLoad() -> void;
  auto ReportPauseChanges() -> void;
  auto LogSessionLeftovers() const -> void;
  auto ReportUnknownStartupCVars() -> void;
  auto ReportGpuWork() -> void;
  //! Publishes `stats` while a client subscribes to it.
  auto ReportStats(const engine::FrameContext& context) -> void;
  auto NotifyMainViewDiscontinuity(vortex::ViewDiscontinuity reason) -> void;
  auto DrawStatus() -> void;

  PlayerLaunch launch_;
  std::filesystem::path launch_path_;
  observer_ptr<control::EventLog> events_;
  //! Plays waiting for the load outcome; shared so a waiting command never
  //! touches the module.
  std::vector<std::shared_ptr<PlayOutcome>> play_waiters_;
  std::vector<std::string> startup_cvars_;

  ViewId main_view_id_ { kInvalidViewId };
  vortex::CompositionView::ViewStateHandle main_view_state_ {
    vortex::CompositionView::kInvalidViewStateHandle
  };
  scene::SceneNode last_camera_;

  //! The scene the next session plays.
  std::string next_scene_;
  //! The scene of the running session.
  std::string session_scene_;
  std::shared_ptr<SessionLoad> load_;
  std::shared_ptr<Remount> remount_;
  //! The scene of the session, once instantiated.
  std::shared_ptr<scene::Scene> scene_;
  bool with_height_fog_ { false };
  bool with_local_fog_ { false };
  bool clear_stage_ { false };
  //! The session ends because its scene failed; keep the failure shown.
  bool session_failed_ { false };
  bool was_paused_ { false };

  //! Shown in the window while there is no scene.
  std::string status_;

  scripting::ScriptDiagnosticHandlerId diagnostic_handler_ {};
  observer_ptr<scripting::ScriptingModule> scripting_ { nullptr };
  vortex::GpuTimelineLease gpu_work_lease_;
  std::uint64_t gpu_work_revision_ { 0U };
  std::optional<std::uint32_t> reported_fps_;
};

} // namespace oxygen::player
