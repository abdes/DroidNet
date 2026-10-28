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
#include "DemoShell/Runtime/PathNormalization.h"
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
#include <Oxygen/Content/IAssetLoader.h>
#include <Oxygen/Core/FrameContext.h>
#include <Oxygen/Core/Transforms/NodeFrame.h>
#include <Oxygen/Core/Types/AssetKey.h>
#include <Oxygen/Core/Types/View.h>
#include <Oxygen/Data/SourceKey.h>
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
  constexpr std::string_view kLooseCookedIndexFileName = "container.index.bin";
  constexpr size_t kSceneInitialCapacity = 10000; // FIXME hack

  auto ToLowerAscii(std::string value) -> std::string
  {
    std::ranges::transform(
      value, value.begin(), [](const unsigned char ch) -> char {
        return static_cast<char>(std::tolower(ch));
      });
    return value;
  }

  auto TryGetLastWriteTime(const std::filesystem::path& path)
    -> std::optional<std::filesystem::file_time_type>
  {
    std::error_code ec;
    const auto write_time = std::filesystem::last_write_time(path, ec);
    if (ec) {
      return std::nullopt;
    }
    return write_time;
  }

  auto IsMountedPak(content::IAssetLoader& asset_loader,
    const std::filesystem::path& pak_path) -> bool
  {
    const auto normalized = runtime::NormalizePath(pak_path);
    const auto mounted = asset_loader.EnumerateMountedSources();
    return std::ranges::any_of(mounted,
      [&normalized](
        const content::IAssetLoader::MountedSourceEntry& source) -> bool {
        return source.source_kind
          == content::IAssetLoader::ContentSourceKind::kPak
          && runtime::NormalizePath(source.source_path) == normalized;
      });
  }

  auto IsMountedLooseRoot(content::IAssetLoader& asset_loader,
    const std::filesystem::path& root_path) -> bool
  {
    const auto normalized = runtime::NormalizePath(root_path);
    const auto mounted = asset_loader.EnumerateMountedSources();
    return std::ranges::any_of(mounted,
      [&normalized](
        const content::IAssetLoader::MountedSourceEntry& source) -> bool {
        return source.source_kind
          == content::IAssetLoader::ContentSourceKind::kLooseCooked
          && runtime::NormalizePath(source.source_path) == normalized;
      });
  }

  auto SceneEntryMatchesStartupToken(
    const ui::SceneEntry& entry, std::string_view token) -> bool
  {
    const auto desired = ToLowerAscii(std::string(token));
    const auto scene_name = ToLowerAscii(entry.name);
    const auto scene_key = ToLowerAscii(data::to_string(entry.key));
    auto scene_stem = std::filesystem::path(entry.name).stem().string();
    scene_stem = ToLowerAscii(std::move(scene_stem));

    return scene_name == desired || scene_stem == desired
      || scene_key == desired || scene_name.contains(desired);
  }

  //! The fallback camera's pose when a scene brings none.
  constexpr auto kFallbackCameraPosition = Vec3 { 0.0F, -6.0F, 3.0F };
  constexpr auto kFallbackCameraPitchDegrees = -20.0F;

  auto LooseIndexPathForRoot(const std::filesystem::path& root_path)
    -> std::filesystem::path
  {
    return root_path / std::string(kLooseCookedIndexFileName);
  }

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
  const auto& demo_root = runtime_paths.showcase;
  shell_config.content_roots = {
    .content_root = runtime_paths.content,
    .cooked_root = demo_root / ".cooked" / "main",
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
      .source_kind = entry.source.kind,
      .source_path = entry.source.path,
      .scene_name = entry.name,
    };
  };
  shell_config.on_scene_load_cancel_requested
    = [this] -> void { scene_load_cancel_requested_ = true; };
  shell_config.on_clear_mounts = [this] -> void {
    pending_source_requests_.push_back(PendingSourceRequest {
      .action = PendingSourceAction::kClear,
    });
  };
  shell_config.on_pak_mounted
    = [this](const std::filesystem::path& path) -> void {
    pending_source_requests_.push_back(PendingSourceRequest {
      .action = PendingSourceAction::kMountPak,
      .path = path,
    });
  };
  shell_config.on_loose_index_loaded
    = [this](const std::filesystem::path& path) -> void {
    pending_source_requests_.push_back(PendingSourceRequest {
      .action = PendingSourceAction::kMountIndex,
      .path = path,
    });
  };

  shell_config.on_generation_published
    = [this](const ui::GenerationPublication& publication) -> void {
    pending_source_requests_.push_back(PendingSourceRequest {
      .action = PendingSourceAction::kMountGeneration,
      .path = publication.cooked_root,
      .previous_root = publication.previous_root,
    });
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

    // The scene is looked for in the mounted sources first; the main library
    // is mounted only when none of them holds it.
    startup_library_index_ = runtime_paths.content / ".cooked" / "main"
      / std::filesystem::path(kLooseCookedIndexFileName);
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
  mounted_pak_write_times_.clear();
  mounted_loose_index_write_times_.clear();
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

  // 1. Process deferred lifecycle actions.
  if (!pending_source_requests_.empty()) {
    bool refresh_library = false;
    while (!pending_source_requests_.empty()) {
      const auto request = std::move(pending_source_requests_.front());
      pending_source_requests_.pop_front();

      const auto action = request.action;
      const auto& path = request.path;
      const char* reason = "source change";
      if (action == PendingSourceAction::kMountPak) {
        reason = "pak mounted";
      } else if (action == PendingSourceAction::kMountIndex) {
        reason = "loose cooked root";
      } else if (action == PendingSourceAction::kClear) {
        reason = "clear mounts";
      }

      if (action == PendingSourceAction::kClear) {
        ReleaseCurrentSceneAsset(reason);
        // IMPORTANT: Never clear/swap scene ownership in OnSceneMutation.
        // Defer to OnFrameStart so FrameContext scene publication remains valid
        // for the whole frame.
        pending_scene_clear_ = true;
      }

      auto asset_loader = Host().engine->GetAssetLoader();
      if (asset_loader) {
        if (action == PendingSourceAction::kClear) {
          asset_loader->ClearMounts();
          mounted_pak_write_times_.clear();
          mounted_loose_index_write_times_.clear();
          refresh_library = true;
        } else if (action == PendingSourceAction::kMountGeneration) {
          const auto normalized = runtime::NormalizePath(path);
          try {
            std::optional<data::SourceKey> previous;
            std::vector<data::SourceKey> older;
            bool selected_is_mounted = false;
            for (const auto& source : asset_loader->EnumerateMountedSources()) {
              const auto mounted = runtime::NormalizePath(source.source_path);
              if (source.source_kind
                  == content::IAssetLoader::ContentSourceKind::kLooseCooked
                && mounted == normalized) {
                selected_is_mounted = true;
              }
              if (source.source_kind
                  == content::IAssetLoader::ContentSourceKind::kLooseCooked
                && mounted != normalized
                && ((request.previous_root
                      && mounted
                        == runtime::NormalizePath(*request.previous_root))
                  || mounted.parent_path() == normalized.parent_path())) {
                previous = source.source_key;
                older.push_back(source.source_key);
              }
            }
            if (selected_is_mounted) {
              previous.reset();
            }
            static_cast<void>(
              asset_loader->MountLooseCookedGeneration(normalized, previous));
            for (const auto key : older) {
              if (!previous || key != *previous) {
                static_cast<void>(
                  asset_loader->RetireLooseCookedGeneration(key));
              }
            }
            if (const auto write_time
              = TryGetLastWriteTime(LooseIndexPathForRoot(normalized))) {
              mounted_loose_index_write_times_.insert_or_assign(
                normalized, *write_time);
            }
            refresh_library = true;
          } catch (const std::system_error& error) {
            if (error.code() == std::errc::device_or_resource_busy) {
              pending_source_requests_.push_front(request);
              break;
            }
            LOG_F(
              ERROR, "Model import generation mount failed: {}", error.what());
          } catch (const std::exception& error) {
            LOG_F(
              ERROR, "Model import generation mount failed: {}", error.what());
          }
        } else if (action == PendingSourceAction::kMountPak) {
          const auto normalized = runtime::NormalizePath(path);
          try {
            const auto already_mounted
              = IsMountedPak(*asset_loader, normalized);
            const auto write_time = TryGetLastWriteTime(normalized);

            if (already_mounted) {
              const auto known_time_it
                = mounted_pak_write_times_.find(normalized);
              const bool has_known_time
                = known_time_it != mounted_pak_write_times_.end();
              const bool file_changed = has_known_time && write_time.has_value()
                && known_time_it->second != *write_time;

              if (file_changed) {
                LOG_F(INFO,
                  "RenderScene: PAK source '{}' changed on disk; refreshing "
                  "mounted source",
                  normalized.string());
                asset_loader->AddPakFile(normalized);
                mounted_pak_write_times_.insert_or_assign(
                  normalized, *write_time);
                refresh_library = true;
              } else {
                LOG_F(INFO,
                  "RenderScene: PAK source '{}' already mounted and unchanged; "
                  "preserving cache",
                  normalized.string());
                if (write_time.has_value() && !has_known_time) {
                  mounted_pak_write_times_.insert_or_assign(
                    normalized, *write_time);
                }
              }
            } else {
              asset_loader->AddPakFile(normalized);
              if (write_time.has_value()) {
                mounted_pak_write_times_.insert_or_assign(
                  normalized, *write_time);
              }
              refresh_library = true;
            }
          } catch (const std::exception& ex) {
            LOG_F(ERROR,
              "RenderScene: Failed to restore/mount persisted PAK source '{}': "
              "{}. Removing persisted entry.",
              normalized.string(), ex.what());
            mounted_pak_write_times_.erase(normalized);
            if (const auto vm = shell.GetContentVm()) {
              vm->PrunePersistedMountedSource(
                ui::SceneSourceKind::kPak, normalized);
            }
            refresh_library = true;
          }
        } else if (action == PendingSourceAction::kMountIndex) {
          const auto root = path.parent_path();
          const auto normalized = runtime::NormalizePath(root);
          const auto normalized_index = LooseIndexPathForRoot(normalized);
          try {
            const auto already_mounted
              = IsMountedLooseRoot(*asset_loader, normalized);
            const auto write_time = TryGetLastWriteTime(normalized_index);

            if (already_mounted) {
              const auto known_time_it
                = mounted_loose_index_write_times_.find(normalized);
              const bool has_known_time
                = known_time_it != mounted_loose_index_write_times_.end();
              const bool file_changed = has_known_time && write_time.has_value()
                && known_time_it->second != *write_time;

              if (file_changed) {
                LOG_F(INFO,
                  "RenderScene: Loose cooked index '{}' changed on disk; "
                  "refreshing mounted source",
                  normalized_index.string());
                asset_loader->AddLooseCookedRoot(normalized);
                mounted_loose_index_write_times_.insert_or_assign(
                  normalized, *write_time);
                refresh_library = true;
              } else if (!has_known_time && write_time.has_value()) {
                LOG_F(INFO,
                  "RenderScene: Loose cooked root '{}' mounted with unknown "
                  "index timestamp; refreshing once to bind latest index",
                  normalized.string());
                asset_loader->AddLooseCookedRoot(normalized);
                mounted_loose_index_write_times_.insert_or_assign(
                  normalized, *write_time);
                refresh_library = true;
              } else {
                LOG_F(INFO,
                  "RenderScene: Loose cooked root '{}' already mounted and "
                  "unchanged; preserving cache",
                  normalized.string());
                if (write_time.has_value() && !has_known_time) {
                  mounted_loose_index_write_times_.insert_or_assign(
                    normalized, *write_time);
                }
              }
            } else {
              asset_loader->AddLooseCookedRoot(normalized);
              if (write_time.has_value()) {
                mounted_loose_index_write_times_.insert_or_assign(
                  normalized, *write_time);
              }
              refresh_library = true;
            }
          } catch (const std::exception& ex) {
            LOG_F(ERROR,
              "RenderScene: Failed to restore/mount persisted loose cooked "
              "source '{}': {}.",
              normalized_index.string(), ex.what());
            mounted_loose_index_write_times_.erase(normalized);
            if (const auto vm = shell.GetContentVm()) {
              vm->PrunePersistedMountedSource(
                ui::SceneSourceKind::kLooseIndex, normalized_index);
            }
            refresh_library = true;
          }
        }
      }
    }

    if (refresh_library) {
      if (const auto vm = shell.GetContentVm()) {
        vm->RefreshLibrary();
        vm->PersistLibraryState();
      }
    }
  }

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
    if (const auto vm = shell.GetContentVm()) {
      for (const auto& scene : vm->GetAvailableScenes()) {
        if (!SceneEntryMatchesStartupToken(scene, *startup_scene_name_)) {
          continue;
        }
        pending_scene_load_ = SceneLoadRequest {
          .key = scene.key,
          .source_kind = scene.source.kind,
          .source_path = scene.source.path,
          .scene_name = scene.name,
        };

        startup_scene_load_requested_ = true;
        LOG_F(INFO,
          "RenderScene: Resolved startup scene override '{}' to scene='{}' "
          "key={} source='{}'",
          *startup_scene_name_, scene.name, data::to_string(scene.key),
          scene.source.path.string());
        break;
      }
    }

    // Sources still mounting may hold the scene.
    if (!startup_scene_load_requested_ && pending_source_requests_.empty()) {
      if (!startup_library_mount_requested_) {
        startup_library_mount_requested_ = true;
        pending_source_requests_.push_back(PendingSourceRequest {
          .action = PendingSourceAction::kMountIndex,
          .path = startup_library_index_,
        });
        LOG_F(INFO,
          "RenderScene: Startup scene override '{}' is in no mounted source; "
          "mounting the main library '{}'",
          *startup_scene_name_, startup_library_index_.string());
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
      try {
        if (request.source_kind == ui::SceneSourceKind::kPak) {
          const auto normalized = runtime::NormalizePath(request.source_path);
          const auto already_mounted = IsMountedPak(*asset_loader, normalized);
          const auto write_time = TryGetLastWriteTime(normalized);

          if (already_mounted) {
            const auto known_time_it
              = mounted_pak_write_times_.find(normalized);
            const bool has_known_time
              = known_time_it != mounted_pak_write_times_.end();
            const bool file_changed = has_known_time && write_time.has_value()
              && known_time_it->second != *write_time;

            if (file_changed) {
              LOG_F(INFO,
                "RenderScene: PAK source '{}' changed on disk for scene "
                "load; refreshing source",
                normalized.string());
              asset_loader->AddPakFile(normalized);
              mounted_pak_write_times_.insert_or_assign(
                normalized, *write_time);
            } else {
              LOG_F(INFO,
                "RenderScene: PAK source '{}' already mounted for scene "
                "load and unchanged; preserving cache",
                normalized.string());
              if (write_time.has_value() && !has_known_time) {
                mounted_pak_write_times_.insert_or_assign(
                  normalized, *write_time);
              }
            }
          } else {
            asset_loader->AddPakFile(normalized);
            if (write_time.has_value()) {
              mounted_pak_write_times_.insert_or_assign(
                normalized, *write_time);
            }
            LOG_F(INFO,
              "RenderScene: Mounted PAK source '{}' for scene load '{}'",
              normalized.string(), request.scene_name);
          }
        } else if (request.source_kind == ui::SceneSourceKind::kLooseIndex) {
          const auto root = request.source_path.parent_path();
          const auto normalized = runtime::NormalizePath(root);
          const auto already_mounted
            = IsMountedLooseRoot(*asset_loader, normalized);
          const auto write_time
            = TryGetLastWriteTime(LooseIndexPathForRoot(normalized));

          if (already_mounted) {
            const auto known_time_it
              = mounted_loose_index_write_times_.find(normalized);
            const bool has_known_time
              = known_time_it != mounted_loose_index_write_times_.end();
            const bool file_changed = has_known_time && write_time.has_value()
              && known_time_it->second != *write_time;

            if (file_changed) {
              LOG_F(INFO,
                "RenderScene: Loose cooked index '{}' changed for scene load; "
                "refreshing mounted source",
                LooseIndexPathForRoot(normalized).string());
              asset_loader->AddLooseCookedRoot(normalized);
              mounted_loose_index_write_times_.insert_or_assign(
                normalized, *write_time);
            } else if (!has_known_time && write_time.has_value()) {
              LOG_F(INFO,
                "RenderScene: Loose cooked root '{}' mounted for scene load "
                "with unknown timestamp; refreshing once",
                normalized.string());
              asset_loader->AddLooseCookedRoot(normalized);
              mounted_loose_index_write_times_.insert_or_assign(
                normalized, *write_time);
            } else {
              LOG_F(INFO,
                "RenderScene: Loose cooked root '{}' already mounted for scene "
                "load and unchanged; preserving cache",
                normalized.string());
              if (write_time.has_value() && !has_known_time) {
                mounted_loose_index_write_times_.insert_or_assign(
                  normalized, *write_time);
              }
            }
          } else {
            asset_loader->AddLooseCookedRoot(normalized);
            if (write_time.has_value()) {
              mounted_loose_index_write_times_.insert_or_assign(
                normalized, *write_time);
            }
            LOG_F(INFO,
              "RenderScene: Mounted loose cooked root '{}' for scene load '{}'",
              normalized.string(), request.scene_name);
          }
        }
      } catch (const std::exception& ex) {
        LOG_F(ERROR,
          "RenderScene: Failed to remount source for scene load (scene='{}' "
          "error='{}')",
          request.scene_name, ex.what());
        if (const auto vm = shell.GetContentVm()) {
          vm->NotifySceneLoadCompleted(request.key, false);
        }
        co_return;
      }

      scene_loader_
        = std::make_shared<SceneLoaderService>(*asset_loader, last_viewport_,
          request.source_kind == ui::SceneSourceKind::kPak
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
            vm->NotifySceneLoadCompleted(swap.scene_key, false);
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
            vm->NotifySceneLoadCompleted(swap.scene_key, false);
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
          vm->NotifySceneLoadCompleted(swap.scene_key, false);
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
          vm->NotifySceneLoadCompleted(*active_scene_load_key_, false);
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
            vm->NotifySceneLoadCompleted(*active_scene_load_key_, false);
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
