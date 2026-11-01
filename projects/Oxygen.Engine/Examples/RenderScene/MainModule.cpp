//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#include "DemoShell/DemoShell.h"
#include "DemoShell/Runtime/ContentMountApplier.h"
#include "DemoShell/Services/DefaultSceneLighting.h"
#include "DemoShell/Services/SceneLoaderService.h"
#include "DemoShell/Services/SkyboxService.h"
#include "DemoShell/UI/ContentVm.h"
#include "RenderScene/MainModule.h"
#include "RenderScene/RuntimePaths.h"
#include <glm/ext/quaternion_float.hpp>
#include <glm/trigonometric.hpp>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Config/PathFinder.h>
#include <Oxygen/Config/PathFinderConfig.h>
#include <Oxygen/Content/ContentMounts.h>
#include <Oxygen/Content/IAssetLoader.h>
#include <Oxygen/Core/FrameContext.h>
#include <Oxygen/Core/Transforms/NodeFrame.h>
#include <Oxygen/Core/Types/AssetKey.h>
#include <Oxygen/Core/Types/View.h>
#include <Oxygen/Data/CookedSource.h>
#include <Oxygen/Engine/AsyncEngine.h>
#include <Oxygen/Host/HostConfig.h>
#include <Oxygen/Host/HostModule.h>
#include <Oxygen/Host/HostWindow.h>
#include <Oxygen/Input/InputSystem.h>
#include <Oxygen/OxCo/Co.h>
#include <Oxygen/Platform/Window.h>
#include <Oxygen/Scene/Camera/Perspective.h>
#include <Oxygen/Scene/Environment/SceneEnvironment.h>
#include <Oxygen/Scene/Environment/SkyLight.h>
#include <Oxygen/Scene/Scene.h>
#include <Oxygen/Vortex/CompositionView.h>

namespace oxygen::examples::render_scene {

namespace {
  constexpr size_t kSceneInitialCapacity = 10000; // FIXME hack

  auto ToLowerAscii(std::string value) -> std::string
  {
    std::ranges::transform(
      value, value.begin(), [](const unsigned char ch) -> char {
        return static_cast<char>(std::tolower(ch));
      });
    return value;
  }

  auto SceneEntryMatchesStartupToken(
    const ui::SceneEntry& entry, std::string_view token) -> bool
  {
    const auto desired = ToLowerAscii(std::string(token));
    const auto virtual_path = ToLowerAscii(entry.virtual_path);
    const auto scene_name = ToLowerAscii(entry.name);
    const auto scene_key = ToLowerAscii(data::to_string(entry.key));

    return scene_name == desired || virtual_path == desired
      || scene_key == desired || virtual_path.contains(desired);
  }

  //! The fallback camera's pose when a scene brings none.
  constexpr auto kFallbackCameraPosition = Vec3 { 0.0F, -6.0F, 3.0F };
  constexpr auto kFallbackCameraPitchDegrees = -20.0F;

} // namespace

MainModule::MainModule(const host::HostContext& host, StartupOptions options)
  : Base(host)
  , last_viewport_({ .width = 0, .height = 0 })
  , startup_(std::move(options))
{
  if (!startup_.scene_name.empty()) {
    startup_scene_name_ = startup_.scene_name;
  }
  // The lifecycle proof toggles the sky light of the startup skybox.
  if (startup_.skybox_path.empty()) {
    startup_.sky_light_lifecycle_proof_enabled = false;
  }
  if (startup_.sky_light_lifecycle_proof_enabled
    && startup_.sky_light_lifecycle_enable_frame
      <= startup_.sky_light_lifecycle_disable_frame) {
    startup_.sky_light_lifecycle_proof_enabled = false;
    LOG_F(WARNING,
      "RenderScene: disabled static SkyLight lifecycle proof because "
      "enable_frame ({}) must be greater than disable_frame ({})",
      startup_.sky_light_lifecycle_enable_frame,
      startup_.sky_light_lifecycle_disable_frame);
  }
}

MainModule::~MainModule() = default;

auto MainModule::BuildWindowProperties() const -> platform::window::Properties
{
  constexpr uint32_t kDefaultWidth = 2560;
  constexpr uint32_t kDefaultHeight = 1400;

  auto props = Base::BuildWindowProperties();
  props.title = "Oxygen :: Examples :: RenderScene";
  props.extent = platform::window::ExtentT {
    .width = kDefaultWidth,
    .height = kDefaultHeight,
  };
  return props;
}

// Setup failures here are unrecoverable; the engine aborts on them.
// NOLINTNEXTLINE(bugprone-exception-escape)
auto MainModule::OnAttachedImpl(observer_ptr<IAsyncEngine> engine) noexcept
  -> std::unique_ptr<DemoShell>
{
  if (!engine) {
    return nullptr;
  }

  auto shell = std::make_unique<DemoShell>();
  auto shell_config = MakeShellConfig();
  const auto runtime_paths = ResolveRuntimePaths();
  shell_config.content_roots = {
    .content_root = runtime_paths.content,
    .cooked_root = runtime_paths.cooked,
    .pak_directory = runtime_paths.paks,
    .scratch_root = runtime_paths.scratch,
  };
  shell_config.panel_config.content_loader = true;
  shell_config.panel_config.camera_controls = true;
  shell_config.panel_config.environment = true;
  shell_config.panel_config.diagnostics = true;
  shell_config.panel_config.post_process = true;
  shell_config.panel_config.ground_grid = true;
  shell_config.enable_camera_rig = true;
  shell_config.force_environment_override = false;
  shell_config.restore_environment_profile = true;
  shell_config.initial_environment_profile = startup_.environment_profile;
  shell_config.startup_skybox_path = startup_.skybox_path;
  shell_config.initial_preview_sun_enabled = startup_.preview_sun_enabled;
  shell_config.preview_scene_ready
    = [this] -> bool { return loaded_scene_active_; };
  shell_config.on_scene_load_requested
    = [this](const ui::SceneEntry& entry) -> void {
    pending_scene_load_ = SceneLoadRequest {
      .key = entry.key,
      .source_kind = entry.source_kind,
      .source_path = entry.source_path,
      .scene_name = entry.name,
    };
  };
  shell_config.on_scene_load_cancel_requested
    = [this] -> void { scene_load_cancel_requested_ = true; };
  shell_config.on_scene_unload_requested = [this] -> void {
    ReleaseCurrentSceneAsset("scene unload");
    // Scene ownership changes at frame start only.
    pending_scene_clear_ = true;
  };
  shell_config.on_mounts_requested
    = [this](std::vector<content::ContentMount> mounts) -> void {
    pending_mounts_ = std::move(mounts);
  };
  shell_config.on_generation_published
    = [this](const runtime::GenerationPublication& publication) -> void {
    pending_generations_.push_back(publication);
  };

  if (!shell->Initialize(shell_config)) {
    LOG_F(WARNING, "RenderScene: DemoShell initialization failed");
    return nullptr;
  }

  if (startup_scene_name_.has_value()) {
    if (const auto vm = shell->GetContentVm(); vm && vm->IsSceneLoading()) {
      vm->CancelSceneLoad();
    }
    pending_scene_load_.reset();
    scene_load_cancel_requested_ = false;
    scene_loader_.reset();
    pending_physics_sidecar_.reset();
    active_scene_load_key_.reset();

    // The scene is looked for in the mounted sources first; the cooked
    // examples are added to the mount list only when none of them holds it.
    startup_cooked_root_ = runtime_paths.cooked;
  }

  StageFallbackScene();

  main_view_id_ = Publisher().GetOrCreateViewId("MainView");
  if (Window() && Window()->GetWindow()) {
    last_viewport_ = Window()->GetWindow()->Size();
  }

  LOG_F(INFO, "RenderScene: DemoShell initialized");
  return shell;
}

void MainModule::OnShutdown() noexcept
{
  auto& shell = GetShell();
  shell.CancelContentImport();
  ReleaseCurrentSceneAsset("module shutdown");
  ClearSceneRuntime("module shutdown");
  Base::OnShutdown();
}

auto MainModule::OnFrameStart(observer_ptr<engine::FrameContext> context)
  -> void
{
  DCHECK_NOTNULL_F(context);
  scene_published_this_frame_ = false;

  // Scene swaps and clears happen at frame start only: the frame context
  // keeps a non-owning scene pointer for the rest of the frame. The host
  // publishes a staged scene in the base frame start; retire the main view's
  // history before, so it cannot carry over to the new scene.
  if (pending_scene_clear_) {
    loaded_scene_active_ = false;
    LOG_F(INFO, "RenderScene: Applying deferred scene clear at frame start");
    ResetMainViewState(context);
    active_scene_ = {};
    main_camera_ = {};
    scene_loader_.reset();
    active_scene_load_key_.reset();
    Stage().Clear();
    StageFallbackScene();
    pending_scene_clear_ = false;
  }
  if (Stage().HasStagedScene()) {
    ResetMainViewState(context);
  }

  Base::OnFrameStart(context);

  if (!HasRenderableWindow()) {
    ResetMainViewState(context);
    return;
  }
  if (Window() && Window()->GetWindow()) {
    last_viewport_ = Window()->GetWindow()->Size();
  }
  if (const auto scene_ptr = Stage().TryGetScene()) {
    ApplySkyLightLifecycleProofToggle(
      *scene_ptr, context->GetFrameSequenceNumber().get());
  }
}

auto MainModule::OnScenePublished(engine::FrameContext& context) -> void
{
  Base::OnScenePublished(context);
  scene_published_this_frame_ = true;
  loaded_scene_active_ = current_scene_key_.has_value();
  active_scene_ = Stage().GetActiveScene();
  main_camera_ = Stage().TakePublishedMainCamera();
  LOG_F(INFO, "RenderScene: Published staged scene at frame start");

  if (const auto load_key
    = std::exchange(active_scene_load_key_, std::nullopt)) {
    auto& shell = GetShell();
    if (const auto vm = shell.GetContentVm()) {
      vm->NotifySceneLoadCompleted(*load_key, true);
    }
    shell.ReapplyPostProcessSettingsToScene();
  }
}

auto MainModule::OnSceneMutation(observer_ptr<engine::FrameContext> context)
  -> co::Co<>
{
  DCHECK_NOTNULL_F(context);
  auto& shell = GetShell();

  // 1. Apply the content panel's mount list and publish new generations.
  co_await ApplyPendingMounts();
  MountPendingGenerations();

  if (startup_scene_name_.has_value() && !startup_scene_load_requested_
    && pending_scene_load_) {
    LOG_F(INFO,
      "RenderScene: Discarding restored scene load '{}' because startup "
      "scene override '{}' is active",
      pending_scene_load_->scene_name, *startup_scene_name_);
    if (const auto vm = shell.GetContentVm(); vm && vm->IsSceneLoading()) {
      vm->CancelSceneLoad();
    }
    pending_scene_load_.reset();
    scene_load_cancel_requested_ = false;
  }

  if (startup_scene_name_.has_value() && !startup_scene_load_requested_
    && !pending_scene_load_ && !scene_loader_) {
    const auto vm = shell.GetContentVm();
    const auto* match = static_cast<const ui::SceneEntry*>(nullptr);
    if (vm) {
      for (const auto& group : vm->Scenes().Groups()) {
        for (const auto& scene : group.scenes) {
          if (match == nullptr
            && SceneEntryMatchesStartupToken(scene, *startup_scene_name_)) {
            match = &scene;
          }
        }
      }
    }
    if (match != nullptr) {
      pending_scene_load_ = SceneLoadRequest {
        .key = match->key,
        .source_kind = match->source_kind,
        .source_path = match->source_path,
        .scene_name = match->name,
      };
      startup_scene_load_requested_ = true;
      LOG_F(INFO,
        "RenderScene: Resolved startup scene override '{}' to scene='{}' "
        "key={} source='{}'",
        *startup_scene_name_, match->virtual_path, data::to_string(match->key),
        match->source_path.string());
    } else if (vm && !vm->IsMounting() && !pending_mounts_) {
      // Sources still mounting may hold the scene.
      if (!startup_cooked_root_added_) {
        startup_cooked_root_added_ = true;
        LOG_F(INFO,
          "RenderScene: Startup scene override '{}' is in no mounted source; "
          "adding the cooked examples '{}'",
          *startup_scene_name_, startup_cooked_root_.string());
        vm->AddSource({
          .kind = data::CookedSourceKind::kLooseCooked,
          .path = startup_cooked_root_,
        });
      } else if (!startup_scene_missing_logged_) {
        startup_scene_missing_logged_ = true;
        LOG_F(WARNING,
          "RenderScene: Startup scene override '{}' did not match any mounted "
          "scene",
          *startup_scene_name_);
      }
    }
  }

  // 2. Process pending scene loads.
  if (scene_load_cancel_requested_) {
    LOG_F(INFO, "RenderScene: Scene load cancellation requested");
    scene_load_cancel_requested_ = false;
    pending_scene_load_.reset();
    scene_loader_.reset();
    pending_physics_sidecar_.reset();
    active_scene_load_key_.reset();
  }

  if (pending_scene_load_) {
    const auto request = *pending_scene_load_;
    pending_scene_load_.reset();

    auto asset_loader = Host().engine->GetAssetLoader();
    if (asset_loader) {
      // The scene's source is mounted by the content panel's mount list.
      scene_loader_
        = std::make_shared<SceneLoaderService>(*asset_loader, last_viewport_,
          request.source_kind == data::CookedSourceKind::kPak
            ? request.source_path
            : std::filesystem::path {},
          observer_ptr { Host().engine.get() }, Host().input_system,
          observer_ptr { &Host().engine->GetScriptCompilationService() },
          PathFinder(std::make_shared<const PathFinderConfig>(
                       Host().engine->GetEngineConfig().path_finder_config),
            std::filesystem::current_path()));
      active_scene_load_key_ = request.key;
      scene_loader_->StartLoad(request.key);
      LOG_F(INFO,
        "RenderScene: Started async scene load (scene_key={} scene='{}')",
        data::to_string(request.key), request.scene_name);
    } else {
      LOG_F(ERROR, "AssetLoader unavailable");
      if (const auto vm = shell.GetContentVm()) {
        vm->NotifySceneLoadCompleted(
          request.key, false, "The engine has no asset loader");
      }
    }
  }

  // 3. Process async scene loading service cleanup.
  if (scene_loader_ && scene_loader_->IsConsumed()) {
    if (scene_loader_->Tick()) {
      scene_loader_.reset();
    }
  }

  if (scene_loader_ && !Stage().HasStagedScene() && !pending_physics_sidecar_) {
    if (scene_loader_->IsReady()) {
      auto loader = scene_loader_;
      auto swap = loader->GetResult();
      LOG_F(INFO, "RenderScene: Building staged scene (scene_key={})",
        data::to_string(swap.scene_key));

      const bool same_scene_source = active_scene_asset_pin_ && swap.asset
        && swap.asset->GetAssetKey() == active_scene_asset_pin_->GetAssetKey()
        && swap.asset->GetSourceKey()
          == active_scene_asset_pin_->GetSourceKey();
      if (!same_scene_source) {
        ReleaseCurrentSceneAsset("scene swap");
      }

      if (swap.asset && loader) {
        DCHECK_F(!Stage().HasStagedScene(),
          "scene build path expects no pre-existing staged scene");
        // Named by its asset, so per-scene settings such as the saved camera
        // pose follow the content rather than the application.
        Stage().Stage(std::make_shared<scene::Scene>(
          data::to_string(swap.scene_key), kSceneInitialCapacity));
        auto staged_scene = Stage().GetStagedScene();
        scene::SceneNode staged_main_camera;
        try {
          staged_main_camera
            = co_await loader->BuildSceneAsync(*staged_scene, swap.asset);
          pending_physics_sidecar_ = swap.physics_asset;
        } catch (const std::exception& ex) {
          Stage().DiscardStagedScene();
          pending_physics_sidecar_.reset();
          LOG_F(ERROR,
            "RenderScene: Scene build/hydration failed (scene_key={} "
            "error='{}')",
            data::to_string(swap.scene_key), ex.what());
          if (const auto vm = shell.GetContentVm()) {
            vm->NotifySceneLoadCompleted(swap.scene_key, false, ex.what());
          }
          active_scene_load_key_.reset();
          scene_loader_ = std::move(loader);
          if (scene_loader_) {
            scene_loader_->MarkConsumed();
          }
          co_return;
        } catch (...) {
          Stage().DiscardStagedScene();
          pending_physics_sidecar_.reset();
          LOG_F(ERROR,
            "RenderScene: Scene build/hydration failed (scene_key={} "
            "error='unknown')",
            data::to_string(swap.scene_key));
          if (const auto vm = shell.GetContentVm()) {
            vm->NotifySceneLoadCompleted(
              swap.scene_key, false, "The scene build failed");
          }
          active_scene_load_key_.reset();
          scene_loader_ = std::move(loader);
          if (scene_loader_) {
            scene_loader_->MarkConsumed();
          }
          co_return;
        }

        Stage().SetStagedMainCamera(std::move(staged_main_camera));
        active_scene_asset_pin_ = swap.asset;
        current_scene_key_ = swap.scene_key;
        LOG_F(INFO,
          "RenderScene: Scene build staged successfully "
          "(scene_key={} physics_sidecar={})",
          data::to_string(swap.scene_key),
          pending_physics_sidecar_ ? "pending" : "none");
      } else {
        LOG_F(ERROR, "RenderScene: Scene build missing asset or loader");
        if (const auto vm = shell.GetContentVm()) {
          vm->NotifySceneLoadCompleted(
            swap.scene_key, false, "The scene asset did not load");
        }
        active_scene_load_key_.reset();
      }

      scene_loader_ = std::move(loader);
      if (scene_loader_ && !pending_physics_sidecar_) {
        scene_loader_->MarkConsumed();
      }
    } else if (scene_loader_->IsFailed()) {
      LOG_F(ERROR, "RenderScene: Scene loading failed");
      if (active_scene_load_key_.has_value()) {
        if (const auto vm = shell.GetContentVm()) {
          vm->NotifySceneLoadCompleted(*active_scene_load_key_, false,
            "The scene or an asset it needs did not load; the engine log names "
            "it");
        }
      }
      active_scene_load_key_.reset();
      scene_loader_.reset();
    } else if (scene_loader_->IsConsumed() && scene_loader_->Tick()) {
      scene_loader_.reset();
    }
  }

  if (pending_physics_sidecar_) {
    const bool can_hydrate_now = active_scene_.IsValid()
      && !Stage().HasStagedScene() && !scene_published_this_frame_
      && scene_loader_ && !scene_loader_->IsFailed();
    if (can_hydrate_now) {
      try {
        co_await scene_loader_->HydratePhysicsSidecar(
          *pending_physics_sidecar_);
        LOG_F(
          INFO, "RenderScene: Deferred physics sidecar hydration completed");
        pending_physics_sidecar_.reset();
        scene_loader_->MarkConsumed();
      } catch (const std::exception& ex) {
        LOG_F(ERROR, "RenderScene: Deferred physics hydration failed: {}",
          ex.what());
        pending_physics_sidecar_.reset();
        if (active_scene_load_key_.has_value()) {
          if (const auto vm = shell.GetContentVm()) {
            vm->NotifySceneLoadCompleted(
              *active_scene_load_key_, false, ex.what());
          }
          active_scene_load_key_.reset();
        }
        scene_loader_->MarkConsumed();
      }
    }
  }

  if (!active_scene_.IsValid() && !Stage().HasStagedScene()) {
    StageFallbackScene();
  }

  co_await Base::OnSceneMutation(context);
  co_return;
}

auto MainModule::ApplyPendingMounts() -> co::Co<>
{
  // A load reads through the mounts; the list waits until it finishes.
  if (!pending_mounts_ || pending_scene_load_ || active_scene_load_key_) {
    co_return;
  }
  auto mounts = std::move(*pending_mounts_);
  pending_mounts_.reset();
  auto outcome = runtime::MountOutcome {};
  if (auto asset_loader = Host().engine->GetAssetLoader()) {
    co_await asset_loader->WaitForPendingLoadsAsync();
    outcome
      = co_await runtime::ApplyContentMounts(*asset_loader, std::move(mounts));
  } else {
    outcome.error = "The engine has no asset loader";
  }
  if (const auto vm = GetShell().GetContentVm()) {
    vm->OnMountsApplied(outcome);
  }
}

auto MainModule::MountPendingGenerations() -> void
{
  auto asset_loader = Host().engine->GetAssetLoader();
  const auto vm = GetShell().GetContentVm();
  while (asset_loader && !pending_generations_.empty()) {
    const auto publication = pending_generations_.front();
    try {
      runtime::MountGeneration(*asset_loader, publication);
    } catch (const std::system_error& error) {
      if (error.code() == std::errc::device_or_resource_busy) {
        // The import's writer still holds it; retried next frame.
        return;
      }
      pending_generations_.pop_front();
      if (vm) {
        vm->OnGenerationMounted(publication, std::string(error.what()));
      }
      continue;
    } catch (const std::exception& error) {
      pending_generations_.pop_front();
      if (vm) {
        vm->OnGenerationMounted(publication, std::string(error.what()));
      }
      continue;
    }
    pending_generations_.pop_front();
    if (vm) {
      vm->OnGenerationMounted(publication, std::nullopt);
    }
  }
}

auto MainModule::ReleaseCurrentSceneAsset(const char* reason) -> void
{
  active_scene_asset_pin_.reset();
  if (!current_scene_key_) {
    return;
  }
  LOG_F(INFO, "RenderScene: Releasing scene asset (reason={} key={})", reason,
    data::to_string(*current_scene_key_));
  last_released_scene_key_ = current_scene_key_;
  current_scene_key_.reset();
}

auto MainModule::BuildViews(
  engine::FrameContext& context, host::HostViews& views) -> void
{
  auto& shell = GetShell();
  // Scripts switch cameras through the scene; view through its active camera.
  if (const auto scene = Stage().TryGetScene(); scene != nullptr) {
    if (const auto active = scene->ActiveCamera();
      active.has_value() && active->GetHandle() != main_camera_.GetHandle()) {
      main_camera_ = *active;
    }
  }
  if (!active_scene_.IsValid() || !main_camera_.IsAlive()
    || !main_camera_.HasCamera()) {
    ResetMainViewState(observer_ptr { &context });
    return;
  }

  // An unset viewport covers the window.
  auto main_comp
    = vortex::CompositionView::ForScene(main_view_id_, View {}, main_camera_);
  main_comp.view_state_handle = main_view_state_
                                  .For(context, ResolveRenderer(),
                                    main_view_id_, active_scene_, main_camera_)
                                  .handle;
  main_comp.with_atmosphere = true;
  main_comp.with_height_fog = shell.IsHeightFogPassRequested();
  main_comp.with_local_fog = shell.IsLocalFogPassRequested();
  views.views.push_back(std::move(main_comp));
  views.main = main_view_id_;
}

auto MainModule::ResetMainViewState(
  const observer_ptr<engine::FrameContext> context) -> void
{
  main_view_state_.Reset(context, ResolveRenderer(), main_view_id_);
}

auto MainModule::ClearSceneRuntime(const char* /*reason*/) -> void
{
  ResetMainViewState();
  active_scene_ = {};
  main_camera_ = {};
  scene_loader_.reset();
  pending_physics_sidecar_.reset();
  active_scene_load_key_.reset();
  Stage().Clear();
}

auto MainModule::StageFallbackScene() -> void
{
  if (Stage().HasStagedScene()) {
    return;
  }

  LOG_F(INFO, "RenderScene: Staging fallback default scene");
  Stage().Stage(
    std::make_shared<scene::Scene>("RenderScene", kSceneInitialCapacity));
  auto staged_scene = Stage().GetStagedScene();
  auto camera_node = staged_scene->CreateNode("MainCamera");
  auto camera = std::make_unique<scene::PerspectiveCamera>();
  const bool attached = camera_node.AttachCamera(std::move(camera));
  CHECK_F(attached, "Failed to attach PerspectiveCamera to MainCamera");
  auto tf = camera_node.GetTransform();
  tf.SetLocalPosition(kFallbackCameraPosition);
  // Face the scene (+Y), pitched down.
  const auto pitch = glm::radians(kFallbackCameraPitchDegrees);
  tf.SetLocalRotation(transforms::LookRotation(
    { .forward = Vec3 { 0.0F, std::cos(pitch), std::sin(pitch) } }));
  Stage().SetStagedMainCamera(std::move(camera_node));
}

auto MainModule::ApplySkyLightLifecycleProofToggle(
  scene::Scene& scene, const std::uint64_t frame_index) -> void
{
  if (!startup_.sky_light_lifecycle_proof_enabled) {
    return;
  }

  const auto toggle_sky_light
    = [&](const bool enabled, const char* action) -> void {
    auto env = scene.GetEnvironment();
    auto sky_light = env != nullptr
      ? env->TryGetSystem<scene::environment::SkyLight>()
      : observer_ptr<scene::environment::SkyLight> {};
    if (sky_light == nullptr) {
      LOG_F(WARNING,
        "RenderScene: Static SkyLight lifecycle proof could not {} SkyLight "
        "at frame {} because the active scene has no SkyLight",
        action, frame_index);
      return;
    }

    sky_light->SetEnabled(enabled);
    scene.Update(false);
    LOG_F(INFO,
      "RenderScene: Static SkyLight lifecycle proof {} SkyLight at frame {}",
      action, frame_index);
  };

  if (!startup_sky_light_lifecycle_disable_applied_
    && frame_index >= startup_.sky_light_lifecycle_disable_frame) {
    startup_sky_light_lifecycle_disable_applied_ = true;
    toggle_sky_light(false, "disabled");
  }

  if (startup_sky_light_lifecycle_disable_applied_
    && !startup_sky_light_lifecycle_enable_applied_
    && frame_index >= startup_.sky_light_lifecycle_enable_frame) {
    startup_sky_light_lifecycle_enable_applied_ = true;
    toggle_sky_light(true, "re-enabled");
  }
}

} // namespace oxygen::examples::render_scene
