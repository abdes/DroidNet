//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
#include <vector>

#include <fmt/format.h>
#include <imgui.h>
#include <nlohmann/json.hpp>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Commands/Command.h>
#include <Oxygen/Commands/CommandRegistry.h>
#include <Oxygen/Commands/Params.h>
#include <Oxygen/Config/PathFinder.h>
#include <Oxygen/Console/Console.h>
#include <Oxygen/Content/ContentMounts.h>
#include <Oxygen/Content/IAssetLoader.h>
#include <Oxygen/Control/EventLog.h>
#include <Oxygen/Core/FrameContext.h>
#include <Oxygen/Core/Types/AssetKey.h>
#include <Oxygen/Core/Types/View.h>
#include <Oxygen/Core/Types/ViewPort.h>
#include <Oxygen/Data/SceneAsset.h>
#include <Oxygen/Engine/AsyncEngine.h>
#include <Oxygen/Host/HostConfig.h>
#include <Oxygen/Host/HostModule.h>
#include <Oxygen/Host/HostWindow.h>
#include <Oxygen/ImGui/ImGuiModule.h>
#include <Oxygen/Input/InputSystem.h>
#include <Oxygen/OxCo/Algorithms.h>
#include <Oxygen/OxCo/Co.h>
#include <Oxygen/OxCo/Event.h>
#include <Oxygen/OxCo/Nursery.h>
#include <Oxygen/Platform/Window.h>
#include <Oxygen/Player/LaunchFile.h>
#include <Oxygen/Player/PlayerModule.h>
#include <Oxygen/Player/PlayerProtocol.h>
#include <Oxygen/Scene/Environment/Fog.h>
#include <Oxygen/Scene/Environment/LocalFogVolume.h>
#include <Oxygen/Scene/Environment/SceneEnvironment.h>
#include <Oxygen/Scene/Scene.h>
#include <Oxygen/Scene/SceneNode.h>
#include <Oxygen/Scene/SceneTraversal.h> // IWYU pragma: keep
#include <Oxygen/Scene/Types/Traversal.h>
#include <Oxygen/SceneInstantiation/SceneInstantiation.h>
#include <Oxygen/Scripting/Attachment/SceneScriptAttachment.h>
#include <Oxygen/Scripting/Module/ScriptingModule.h>
#include <Oxygen/Scripting/Resolver/ScriptSourceResolver.h>
#include <Oxygen/Vortex/CompositionView.h>
#include <Oxygen/Vortex/Diagnostics/DiagnosticsService.h>
#include <Oxygen/Vortex/Diagnostics/GpuWorkSummary.h>
#include <Oxygen/Vortex/Renderer.h>
#include <Oxygen/Vortex/Types/ExposureTransition.h>

namespace oxygen::player {

namespace {

  constexpr std::string_view kSessionBeginCommand = "ngin.session.begin";
  constexpr std::size_t kMinSceneCapacity = 4096U;
  constexpr std::string_view kStoppedStatus
    = "Stopped. Press Play to run the scene again.";

  //! Whether the scene's height fog renders in the main pass.
  auto IsHeightFogRequested(const scene::Scene& scene) -> bool
  {
    const auto environment = scene.GetEnvironment();
    if (environment == nullptr) {
      return false;
    }
    const auto fog = environment->TryGetSystem<scene::environment::Fog>();
    return fog && fog->IsEnabled() && fog->GetRenderInMainPass();
  }

  //! Whether any enabled local fog volume exists; the renderer gathers the
  //! volumes only for views that request local fog.
  auto HasEnabledLocalFog(const scene::Scene& scene) -> bool
  {
    auto found = false;
    std::ignore = scene.Traverse().Traverse(
      [&found](const scene::ConstVisitedNode& visited,
        const bool dry_run) -> scene::VisitResult {
        if (dry_run || visited.node_impl == nullptr
          || !visited.node_impl
            ->HasComponent<scene::environment::LocalFogVolume>()) {
          return scene::VisitResult::kContinue;
        }
        found = visited.node_impl
                  ->GetComponent<scene::environment::LocalFogVolume>()
                  .IsEnabled();
        return found ? scene::VisitResult::kStop
                     : scene::VisitResult::kContinue;
      });
    return found;
  }

  //! What a session task needs; everything it points to outlives the task.
  struct LoadInputs {
    observer_ptr<content::IAssetLoader> loader;
    observer_ptr<scripting::IScriptCompilationService> compiler;
    observer_ptr<engine::InputSystem> input;
    observer_ptr<control::EventLog> events;
    PathFinder path_finder;
    std::string scene_path;
  };

  template <typename T> struct PendingLoad {
    std::shared_ptr<T> asset;
    co::Event done;
  };

  auto FindMountedScene(const content::IAssetLoader& loader,
    const std::string_view virtual_path) -> std::optional<data::AssetKey>
  {
    for (const auto& entry : loader.EnumerateMountedScenes()) {
      if (entry.virtual_path == virtual_path) {
        return entry.scene_key;
      }
    }
    return std::nullopt;
  }

  auto SceneName(const std::string_view virtual_path) -> std::string
  {
    return std::filesystem::path(virtual_path).stem().string();
  }

} // namespace

//! A session's load and instantiation, shared with the task that runs it.
struct SessionLoad {
  //! Triggered to cancel the task at its suspension point.
  co::Event cancel;
  bool finished { false };
  //! The instantiated scene; null when the load failed.
  std::shared_ptr<scene::Scene> scene;
  //! Why the load failed.
  std::string failure;
  std::vector<instantiation::SceneInstantiationIssue> issues;
  //! Script attach problems, reported once the scene is loaded.
  std::vector<scripting::ScriptDiagnostic> diagnostics;
};

//! A Play waiting for its load outcome.
struct PlayOutcome {
  co::Event done;
  commands::CommandResult result;
};

//! A mount replacement before a play.
struct Remount {
  bool finished { false };
  std::string failure;
  std::string scene;
};

namespace {

  // NOLINTBEGIN(*-avoid-reference-coroutine-parameters)
  auto LoadScene(const LoadInputs& in, const std::shared_ptr<SessionLoad>& load)
    -> co::Co<>
  // NOLINTEND(*-avoid-reference-coroutine-parameters)
  {
    const auto key = FindMountedScene(*in.loader, in.scene_path);
    if (!key.has_value()) {
      load->failure
        = fmt::format("no mounted content has a scene at '{}'", in.scene_path);
      co_return;
    }
    auto pending = std::make_shared<PendingLoad<data::SceneAsset>>();
    in.loader->StartLoadScene(
      *key, [pending](std::shared_ptr<data::SceneAsset> asset) -> void {
        pending->asset = std::move(asset);
        pending->done.Trigger();
      });
    co_await pending->done;
    if (!pending->asset) {
      load->failure
        = fmt::format("the scene '{}' could not be loaded", in.scene_path);
      co_return;
    }

    const auto capacity
      = (std::max)(kMinSceneCapacity, pending->asset->GetNodes().size() * 2U);
    auto scene
      = std::make_shared<scene::Scene>(SceneName(in.scene_path), capacity);
    // The scene is staged only once built; collect its mutations from the
    // start so observers that register on publication receive them.
    scene->CollectMutationsStart();

    const auto resolver = scripting::ScriptSourceResolver(in.path_finder);
    const auto scripts
      = scripting::SceneScriptAttachment(*in.loader, resolver, in.compiler,
        [events = in.events](const scripting::ScriptDiagnostic& d) -> void {
          events->Publish("diagnostic", DiagnosticData(d));
        });
    auto instance = co_await instantiation::Instantiate(
      instantiation::SceneInstantiationRequest {
        .asset = std::move(pending->asset),
        .target = observer_ptr { scene.get() },
        .scripts = observer_ptr { &scripts },
        .input = in.input,
      },
      *in.loader);

    load->issues = std::move(instance.issues);
    for (auto& outcome : instance.scripts) {
      for (auto& diagnostic : outcome.diagnostics) {
        load->diagnostics.push_back(std::move(diagnostic));
      }
    }
    if (!scene->ActiveCamera().has_value()) {
      load->failure = "the scene has no camera to view it through";
      co_return;
    }
    load->scene = std::move(scene);
  }

  auto RunSessionLoad(LoadInputs in, std::shared_ptr<SessionLoad> load)
    -> co::Co<>
  {
    try {
      co_await co::AnyOf(LoadScene(in, load), load->cancel);
    } catch (const std::exception& ex) {
      load->scene.reset();
      load->failure = fmt::format("the scene failed to load: {}", ex.what());
    }
    load->finished = true;
  }

  auto RunRemount(const observer_ptr<content::IAssetLoader> loader,
    std::vector<content::ContentMount> mounts, std::shared_ptr<Remount> remount)
    -> co::Co<>
  {
    try {
      // No session runs, so nothing starts new loads until the commit.
      co_await loader->WaitForPendingLoadsAsync();
      auto prepared = co_await loader->PrepareMountsAsync(std::move(mounts));
      co_await loader->WaitForPendingLoadsAsync();
      auto retirement = loader->CommitPreparedMounts(std::move(prepared));
      retirement.Finish();
    } catch (const std::exception& ex) {
      remount->failure = fmt::format(
        "the cooked content could not be remounted: {}", ex.what());
    }
    remount->finished = true;
  }

} // namespace

PlayerModule::PlayerModule(const host::HostContext& host, PlayerLaunch launch,
  std::filesystem::path launch_path, std::vector<std::string> startup_cvars)
  : HostModule(host)
  , launch_(std::move(launch))
  , launch_path_(std::move(launch_path))
  , events_(host.events.get())
  , startup_cvars_(std::move(startup_cvars))
  , next_scene_(launch_.scene)
{
  CHECK_NOTNULL_F(events_.get());
}

PlayerModule::~PlayerModule() = default;

auto PlayerModule::BuildWindowProperties() const -> platform::window::Properties
{
  auto props = HostModule::BuildWindowProperties();
  props.title = launch_.window.title;
  if (launch_.window.mode == LaunchWindowMode::kBorderless) {
    // The desktop mode: no display mode is selected, so no framebuffer size.
    props.flags.full_screen = true;
  } else {
    props.framebuffer_extent = {
      .width = launch_.window.width,
      .height = launch_.window.height,
    };
  }
  return props;
}

auto PlayerModule::OnHostAttached(const observer_ptr<IAsyncEngine> engine)
  -> bool
{
  const auto loader = engine->GetAssetLoader();
  if (!loader) {
    LOG_F(ERROR, "{}: the engine has no asset loader", GetName());
    return false;
  }
  for (const auto& mount : launch_.mounts) {
    if (mount.kind == LaunchMount::Kind::kPakFile) {
      loader->AddPakFile(mount.path);
    } else {
      loader->AddLooseCookedRoot(mount.path);
    }
    LOG_F(INFO, "{}: mounted '{}'", GetName(), mount.path.string());
  }

  main_view_id_ = Publisher().GetOrCreateViewId("MainView");
  main_view_state_ = vortex::CompositionView::ViewStateHandle { 1U };

  if (const auto scripting
    = Host().engine->GetModule<scripting::ScriptingModule>()) {
    scripting_ = observer_ptr { &scripting->get() };
    diagnostic_handler_ = scripting_->AddDiagnosticHandler(
      [events = events_](
        const scripting::ScriptDiagnostic& diagnostic) -> void {
        events->Publish("diagnostic", DiagnosticData(diagnostic));
      });
  }

  // The player plays as soon as the engine runs.
  status_ = fmt::format("Loading {}", next_scene_);
  static_cast<void>(engine->BeginSession());
  return true;
}

auto PlayerModule::OnShutdown() noexcept -> void
{
  try {
    ResolvePlay(commands::CommandResult::Failure(
      commands::ExecutionStatus::kCancelled, "the engine is shutting down"));
    Publish(LifecycleState::kStopping);
  } catch (...) {
    // Shutdown continues without the replies.
    play_waiters_.clear();
  }
  if (load_) {
    load_->cancel.Trigger();
    load_.reset();
  }
  remount_.reset();
  gpu_work_lease_.Release();
  if (scripting_) {
    static_cast<void>(scripting_->RemoveDiagnosticHandler(diagnostic_handler_));
    scripting_ = nullptr;
  }
  scene_.reset();
  last_camera_ = {};
  HostModule::OnShutdown();
}

auto PlayerModule::RegisterCommands(
  const observer_ptr<commands::CommandRegistry> registry) noexcept -> void
{
  HostModule::RegisterCommands(registry);
  try {
    static_cast<void>(registry->Register(commands::CommandDefinition {
      .name = std::string(kSessionBeginCommand),
      .label = "Play",
      .help = "Read the launch file again, remount its content and play "
              "its scene, or the scene at the given virtual path",
      .params = {
        commands::CommandParam {
          .name = "scene",
          .type = commands::ParamType::kString,
          .help = "Scene virtual path; the launch file's scene when empty",
          .default_value = commands::ParamValue { std::string {} },
        },
      },
      // Replies once the scene has loaded or failed. The waiting coroutine
      // holds only the shared outcome, never the module.
      .async_handler
      = [this](commands::CommandArgs args,
          commands::CommandContext) -> co::Co<commands::CommandResult> {
        const auto& scene = args.GetString("scene");
        auto started = HandlePlay(
          scene.empty() ? std::nullopt : std::optional<std::string>(scene));
        if (started.status != commands::ExecutionStatus::kOk) {
          co_return started;
        }
        const auto outcome = std::make_shared<PlayOutcome>();
        play_waiters_.push_back(outcome);
        co_await outcome->done;
        co_return outcome->result;
      },
      .result_schema = nlohmann::json {
        { "type", "object" },
        { "properties",
          {
            { "scene", { { "type", "string" } } },
            { "camera", { { "type", "string" } } },
            { "issues", { { "type", "array" } } },
          }, },
      },
    }));
  } catch (const std::exception& ex) {
    LOG_F(ERROR, "{}: command registration failed: {}", GetName(), ex.what());
  }
}

auto PlayerModule::OnSessionBegin(
  const observer_ptr<IAsyncEngine> engine) noexcept -> bool
{
  try {
    const auto nursery = Host().nursery;
    if (!nursery || nursery->GetExecutor() == nullptr) {
      LOG_F(ERROR, "{}: no task nursery to load the scene in", GetName());
      return false;
    }
    session_scene_ = next_scene_;
    was_paused_ = false;
    status_ = fmt::format("Loading {}", session_scene_);
    Publish(LifecycleState::kSessionStarted);
    LOG_F(INFO, "{}: session started on '{}'", GetName(), session_scene_);

    load_ = std::make_shared<SessionLoad>();
    nursery->Start(&RunSessionLoad,
      LoadInputs {
        .loader = engine->GetAssetLoader(),
        .compiler = observer_ptr { &engine->GetScriptCompilationService() },
        .input = Host().input_system,
        .events = events_,
        .path_finder = engine->GetPathFinder(),
        .scene_path = session_scene_,
      },
      load_);
    return true;
  } catch (const std::exception& ex) {
    LOG_F(ERROR, "{}: the session could not begin: {}", GetName(), ex.what());
  } catch (...) {
    LOG_F(ERROR, "{}: the session could not begin", GetName());
  }
  load_.reset();
  return false;
}

auto PlayerModule::OnSessionEnd(
  [[maybe_unused]] const observer_ptr<IAsyncEngine> engine) noexcept -> void
{
  try {
    if (load_) {
      // The task stops at its suspension point; a loader callback that
      // fires later only completes the task's own pending block.
      load_->cancel.Trigger();
      load_.reset();
    }
    NotifyMainViewDiscontinuity(vortex::ViewDiscontinuity::kWorldReplacement);
    scene_.reset();
    last_camera_ = {};
    clear_stage_ = true;
    if (!session_failed_) {
      status_ = std::string(kStoppedStatus);
      // A session ended before its scene loaded never produced an outcome.
      ResolvePlay(commands::CommandResult::Failure(
        commands::ExecutionStatus::kCancelled, "the session ended"));
    }
    session_failed_ = false;
    Publish(LifecycleState::kSessionEnded);
    LOG_F(INFO, "{}: session ended", GetName());
  } catch (const std::exception& ex) {
    LOG_F(ERROR, "{}: session end failed: {}", GetName(), ex.what());
  } catch (...) {
    LOG_F(ERROR, "{}: session end failed", GetName());
  }
}

auto PlayerModule::OnFrameStart(
  const observer_ptr<engine::FrameContext> context) -> void
{
  if (clear_stage_) {
    // The scene stays published through the session end hooks; the player
    // shows no scene after a session.
    Stage().Clear();
    clear_stage_ = false;
    LogSessionLeftovers();
  }
  ReportUnknownStartupCVars();
  FinishRemount();
  FinishLoad();
  ReportPauseChanges();
  UpdateGpuWorkLease();
  ReportGpuWork();
  ReportStats(*context);
  HostModule::OnFrameStart(context);
}

auto PlayerModule::LogSessionLeftovers() const -> void
{
  // Every session leaves the same counts behind; a growing count is a leak.
  const auto loader = Host().engine->GetAssetLoader();
  const auto input = Host().input_system;
  LOG_F(INFO,
    "{}: after the session: {} input actions, {} mapping contexts, "
    "{} mounted sources",
    GetName(), input ? input->GetActionCount() : 0U,
    input ? input->GetMappingContextCount() : 0U,
    loader ? loader->EnumerateMountedSources().size() : 0U);
}

auto PlayerModule::ReportUnknownStartupCVars() -> void
{
  // Every module registered its variables before the first frame.
  const auto& console = Host().engine->GetConsole();
  for (const auto& name : startup_cvars_) {
    if (!console.FindCVar(name)) {
      LOG_F(WARNING, "--cvar {}: no console variable has this name", name);
    }
  }
  startup_cvars_.clear();
}

auto PlayerModule::HandlePlay(const std::optional<std::string>& scene)
  -> commands::CommandResult
{
  if (Host().engine->GetSessionState() != IAsyncEngine::SessionState::kNone
    || remount_) {
    return commands::CommandResult::Failure(
      commands::ExecutionStatus::kError, "a play session is already running");
  }
  auto current = PlayerLaunch {};
  try {
    current = ReadLaunchFile(launch_path_);
  } catch (const LaunchFileError& ex) {
    return commands::CommandResult::Failure(
      commands::ExecutionStatus::kError, ex.what());
  }
  // Window, control and script source roots belong to the process.
  launch_.mounts = std::move(current.mounts);
  launch_.scene = std::move(current.scene);
  next_scene_ = scene.value_or(launch_.scene);
  StartRemount();
  return commands::CommandResult::Success();
}

auto PlayerModule::StartRemount() -> void
{
  const auto nursery = Host().nursery;
  const auto loader = Host().engine->GetAssetLoader();
  if (!nursery || nursery->GetExecutor() == nullptr || !loader) {
    return;
  }
  remount_ = std::make_shared<Remount>();
  remount_->scene = next_scene_;
  status_ = fmt::format("Loading {}", next_scene_);
  nursery->Start(&RunRemount, loader, launch_.mounts, remount_);
}

auto PlayerModule::FinishRemount() -> void
{
  if (!remount_ || !remount_->finished) {
    return;
  }
  const auto remount = std::exchange(remount_, nullptr);
  if (!remount->failure.empty()) {
    LOG_F(ERROR, "{}: {}", GetName(), remount->failure);
    status_ = remount->failure;
    session_scene_ = remount->scene;
    Publish(LifecycleState::kSceneFailed, remount->failure);
    Publish(LifecycleState::kSessionEnded);
    auto failure = commands::CommandResult::Failure(
      commands::ExecutionStatus::kError, remount->failure);
    failure.data = { { "scene", remount->scene } };
    ResolvePlay(failure);
    return;
  }
  LOG_F(INFO, "{}: remounted the cooked content", GetName());
  static_cast<void>(Host().engine->BeginSession());
}

auto PlayerModule::FinishLoad() -> void
{
  if (!load_ || !load_->finished) {
    return;
  }
  const auto load = std::exchange(load_, nullptr);
  if (!load->scene) {
    LOG_F(ERROR, "{}: {}", GetName(), load->failure);
    status_ = load->failure;
    session_failed_ = true;
    Publish(LifecycleState::kSceneFailed, load->failure, load->issues);
    auto failure = commands::CommandResult::Failure(
      commands::ExecutionStatus::kError, load->failure);
    failure.data = LifecycleData(
      LifecycleState::kSceneFailed, session_scene_, {}, load->issues);
    ResolvePlay(failure);
    Host().engine->EndSession();
    return;
  }
  scene_ = load->scene;
  Stage().Stage(scene_);
  with_height_fog_ = IsHeightFogRequested(*scene_);
  with_local_fog_ = HasEnabledLocalFog(*scene_);
  status_.clear();
  Publish(LifecycleState::kSceneLoaded, {}, load->issues);
  for (const auto& diagnostic : load->diagnostics) {
    events_->Publish("diagnostic", DiagnosticData(diagnostic));
  }
  const auto camera = scene_->ActiveCamera();
  const auto camera_name
    = camera.has_value() ? camera->GetName() : std::string {};
  LOG_F(INFO, "{}: scene '{}' loaded with {} issue(s), viewed through '{}'",
    GetName(), session_scene_, load->issues.size(), camera_name);
  auto loaded = LifecycleData(
    LifecycleState::kSceneLoaded, session_scene_, {}, load->issues);
  loaded["camera"] = camera_name;
  ResolvePlay(commands::CommandResult::Success(
    fmt::format("playing {} through '{}'", session_scene_, camera_name),
    std::move(loaded)));
}

auto PlayerModule::ReportPauseChanges() -> void
{
  const auto& engine = Host().engine;
  if (engine->GetSessionState() == IAsyncEngine::SessionState::kNone) {
    was_paused_ = false;
    return;
  }
  const auto paused = engine->IsPaused();
  if (paused != was_paused_) {
    was_paused_ = paused;
    Publish(paused ? LifecycleState::kPaused : LifecycleState::kResumed);
  }
}

auto PlayerModule::ResolvePlay(const commands::CommandResult& result) -> void
{
  for (const auto& waiter : std::exchange(play_waiters_, {})) {
    waiter->result = result;
    waiter->done.Trigger();
  }
}

auto PlayerModule::Publish(const LifecycleState state,
  const std::string_view message,
  const std::span<const instantiation::SceneInstantiationIssue> issues) -> void
{
  events_->Publish(
    "lifecycle", LifecycleData(state, session_scene_, message, issues));
}

auto PlayerModule::UpdateGpuWorkLease() -> void
{
  // A remote client subscribed to `gpu_work` holds the timeline lease.
  const bool wanted = events_->HasInterest("gpu_work");
  if (!wanted) {
    gpu_work_lease_.Release();
    return;
  }
  if (gpu_work_lease_.IsActive()) {
    return;
  }
  if (const auto renderer = ResolveRenderer()) {
    gpu_work_lease_ = renderer->GetDiagnosticsService().AcquireGpuTimeline();
    gpu_work_revision_ = 0U;
  }
}

auto PlayerModule::ReportStats(const engine::FrameContext& context) -> void
{
  if (!events_->HasInterest(kStatsTopic)) {
    reported_fps_.reset();
    return;
  }
  // The engine's frame rate is sampled and held, so it changes a few times a
  // second at most; whole frames per second are all a status shows.
  const auto fps = static_cast<std::uint32_t>(
    std::lround(context.GetModuleTimingData().current_fps));
  if (reported_fps_ == fps) {
    return;
  }
  reported_fps_ = fps;
  events_->Publish(kStatsTopic, StatsData(fps));
}

auto PlayerModule::ReportGpuWork() -> void
{
  if (!gpu_work_lease_.IsActive()) {
    return;
  }
  const auto renderer = ResolveRenderer();
  if (!renderer) {
    return;
  }
  const auto summary
    = renderer->GetDiagnosticsService().GetLatestGpuWorkSummary();
  if (summary && summary->revision != gpu_work_revision_) {
    gpu_work_revision_ = summary->revision;
    events_->Publish("gpu_work", GpuWorkData(*summary));
  }
}

auto PlayerModule::NotifyMainViewDiscontinuity(
  const vortex::ViewDiscontinuity reason) -> void
{
  if (const auto renderer = ResolveRenderer()) {
    // An unknown view (never published) has no exposure to restart.
    static_cast<void>(
      renderer->NotifyViewDiscontinuity(main_view_state_, reason));
  }
}

auto PlayerModule::BuildViews(
  engine::FrameContext& /*context*/, host::HostViews& views) -> void
{
  if (!scene_ || Stage().TryGetScene().get() != scene_.get()) {
    return;
  }
  const auto camera = scene_->ActiveCamera();
  if (!camera.has_value()) {
    status_ = "The scene has no active camera.";
    return;
  }
  const auto window = Window();
  if (!window || !window->GetWindow()) {
    return;
  }
  const auto extent = window->GetWindow()->Size();
  if (extent.width == 0U || extent.height == 0U) {
    return;
  }
  if (last_camera_.IsAlive()
    && last_camera_.GetHandle() != camera->GetHandle()) {
    NotifyMainViewDiscontinuity(vortex::ViewDiscontinuity::kCameraCut);
  }
  last_camera_ = *camera;
  status_.clear();

  auto view = View {};
  view.viewport = ViewPort {
    .top_left_x = 0.0F,
    .top_left_y = 0.0F,
    .width = static_cast<float>(extent.width),
    .height = static_cast<float>(extent.height),
    .min_depth = 0.0F,
    .max_depth = 1.0F,
  };
  auto main_view
    = vortex::CompositionView::ForScene(main_view_id_, view, *camera);
  main_view.name = "MainView";
  main_view.view_state_handle = main_view_state_;
  main_view.with_atmosphere = true;
  main_view.with_height_fog = with_height_fog_;
  main_view.with_local_fog = with_local_fog_;
  views.views.push_back(std::move(main_view));
  views.main = main_view_id_;
}

auto PlayerModule::RetainInactiveView(const ViewId view_id) const noexcept
  -> bool
{
  // The main view's state and framebuffers outlive sessions.
  return view_id == main_view_id_;
}

auto PlayerModule::OnGuiUpdate(const observer_ptr<engine::FrameContext> context)
  -> co::Co<>
{
  co_await HostModule::OnGuiUpdate(context);
  const auto imgui = ResolveImGuiModule();
  if (status_.empty() || !imgui || !imgui->IsFrameActive()
    || imgui->GetImGuiContext() == nullptr) {
    co_return;
  }
  ImGui::SetCurrentContext(imgui->GetImGuiContext());
  DrawStatus();
}

auto PlayerModule::DrawStatus() -> void
{
  const auto* viewport = ImGui::GetMainViewport();
  const auto center = ImVec2 {
    viewport->WorkPos.x + (viewport->WorkSize.x * 0.5F),
    viewport->WorkPos.y + (viewport->WorkSize.y * 0.5F),
  };
  ImGui::SetNextWindowPos(center, ImGuiCond_Always, ImVec2 { 0.5F, 0.5F });
  constexpr auto kFlags = static_cast<ImGuiWindowFlags>(
    static_cast<unsigned>(ImGuiWindowFlags_NoDecoration)
    | static_cast<unsigned>(ImGuiWindowFlags_AlwaysAutoResize)
    | static_cast<unsigned>(ImGuiWindowFlags_NoSavedSettings)
    | static_cast<unsigned>(ImGuiWindowFlags_NoFocusOnAppearing)
    | static_cast<unsigned>(ImGuiWindowFlags_NoNav)
    | static_cast<unsigned>(ImGuiWindowFlags_NoInputs));
  if (ImGui::Begin("##PlayerStatus", nullptr, kFlags)) {
    ImGui::TextUnformatted(status_.c_str());
  }
  ImGui::End();
}

} // namespace oxygen::player
