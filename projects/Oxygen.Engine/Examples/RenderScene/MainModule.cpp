//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <exception>
#include <filesystem>
#include <source_location>
#include <string_view>

#include <glm/gtc/quaternion.hpp>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Base/StringUtils.h>
#include <Oxygen/Config/PathFinder.h>
#include <Oxygen/Config/PathFinderConfig.h>
#include <Oxygen/Content/AssetLoader.h>
#include <Oxygen/Content/IAssetLoader.h>
#include <Oxygen/Content/InputContextHydration.h>
#include <Oxygen/Core/FrameContext.h>
#include <Oxygen/Data/AssetKey.h>
#include <Oxygen/Data/InputMappingContextAsset.h>
#include <Oxygen/Engine/AsyncEngine.h>
#include <Oxygen/Graphics/Common/Texture.h>
#include <Oxygen/Input/InputMappingContext.h>
#include <Oxygen/Input/InputSystem.h>
#include <Oxygen/Renderer/ImGui/ImGuiModule.h>
#include <Oxygen/Renderer/Internal/SkyAtmosphereLutManager.h>
#include <Oxygen/Renderer/Pipeline/CompositionView.h>
#include <Oxygen/Renderer/Pipeline/ForwardPipeline.h>
#include <Oxygen/Renderer/Renderer.h>
#include <Oxygen/Scene/Camera/Perspective.h>
#include <Oxygen/Scene/Scene.h>

#include "DemoShell/DemoShell.h"
#include "DemoShell/Runtime/DemoAppContext.h"
#include "DemoShell/Runtime/PathNormalization.h"
#include "DemoShell/Services/SceneLoaderService.h"
#include "DemoShell/UI/ContentVm.h"
#include "RenderScene/MainModule.h"

using oxygen::scene::SceneNodeFlags;

namespace oxygen::examples::render_scene {

namespace {
  constexpr std::string_view kLooseCookedIndexFileName = "container.index.bin";
  constexpr size_t kSceneInitialCapacity = 10000; // FIXME hack

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
      [&normalized](const content::IAssetLoader::MountedSourceEntry& source) {
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
      [&normalized](const content::IAssetLoader::MountedSourceEntry& source) {
        return source.source_kind
          == content::IAssetLoader::ContentSourceKind::kLooseCooked
          && runtime::NormalizePath(source.source_path) == normalized;
      });
  }

  auto LooseIndexPathForRoot(const std::filesystem::path& root_path)
    -> std::filesystem::path
  {
    return root_path / std::string(kLooseCookedIndexFileName);
  }

  auto CollectMountedSourceKeysForPath(
    const content::IAssetLoader& asset_loader,
    const content::IAssetLoader::ContentSourceKind source_kind,
    const std::filesystem::path& source_path) -> std::vector<data::SourceKey>
  {
    const auto normalized = runtime::NormalizePath(source_path);
    auto keys = std::vector<data::SourceKey> {};
    for (const auto& source : asset_loader.EnumerateMountedSources()) {
      if (source.source_kind != source_kind) {
        continue;
      }
      if (runtime::NormalizePath(source.source_path) != normalized) {
        continue;
      }
      keys.push_back(source.source_key);
    }
    return keys;
  }

  auto SourceKeyMatchesAny(const data::SourceKey& source_key,
    const std::vector<data::SourceKey>& source_keys) -> bool
  {
    return std::ranges::any_of(source_keys,
      [&](const data::SourceKey& key) { return key == source_key; });
  }

  auto HydrateMountedInputContextsForSource(content::AssetLoader& asset_loader,
    observer_ptr<engine::InputSystem> input_system,
    const content::IAssetLoader::ContentSourceKind source_kind,
    const std::filesystem::path& source_path) -> co::Co<>
  {
    if (!input_system) {
      co_return;
    }

    const auto source_keys
      = CollectMountedSourceKeysForPath(asset_loader, source_kind, source_path);
    if (source_keys.empty()) {
      co_return;
    }

    constexpr auto kAutoLoadMask = static_cast<uint32_t>(
      data::pak::input::InputMappingContextFlags::kAutoLoad);
    constexpr auto kAutoActivateMask = static_cast<uint32_t>(
      data::pak::input::InputMappingContextFlags::kAutoActivate);

    for (const auto& entry : asset_loader.EnumerateMountedInputContexts()) {
      if (!SourceKeyMatchesAny(entry.source_key, source_keys)) {
        continue;
      }

      const auto flags = static_cast<uint32_t>(entry.flags);
      if ((flags & kAutoLoadMask) == 0U) {
        continue;
      }

      auto context_asset
        = co_await asset_loader.LoadAssetAsync<data::InputMappingContextAsset>(
          entry.asset_key);
      if (!context_asset) {
        LOG_F(WARNING,
          "RenderScene: Failed to load mounted input context asset {}",
          data::to_string(entry.asset_key));
        continue;
      }

      auto hydrated = content::HydrateInputContext(
        *context_asset, asset_loader, *input_system);
      if (!hydrated) {
        LOG_F(WARNING,
          "RenderScene: Failed to hydrate mounted input context asset {}",
          data::to_string(entry.asset_key));
        (void)asset_loader.ReleaseAsset(entry.asset_key);
        continue;
      }

      if (!input_system->GetMappingContextByName(hydrated->GetName())) {
        input_system->AddMappingContext(hydrated, entry.default_priority);
      }
      if ((flags & kAutoActivateMask) != 0U) {
        input_system->ActivateMappingContext(hydrated);
      }

      (void)asset_loader.ReleaseAsset(entry.asset_key);
    }

    co_return;
  }

} // namespace

MainModule::MainModule(const examples::DemoAppContext& app)
  : Base(app)
  , last_viewport_({ 0, 0 })
{
}

MainModule::~MainModule() = default;

auto MainModule::BuildDefaultWindowProperties() const
  -> platform::window::Properties
{
  constexpr uint32_t kDefaultWidth = 2560;
  constexpr uint32_t kDefaultHeight = 1400;

  platform::window::Properties props("Oxygen :: Examples :: RenderScene");
  props.extent = platform::window::ExtentT { .width = kDefaultWidth,
    .height = kDefaultHeight };
  props.flags = { .hidden = false,
    .always_on_top = false,
    .full_screen = app_.fullscreen,
    .maximized = false,
    .minimized = false,
    .resizable = true,
    .borderless = false };
  return props;
}

auto MainModule::OnAttachedImpl(observer_ptr<IAsyncEngine> engine) noexcept
  -> std::unique_ptr<DemoShell>
{
  if (!engine) {
    return nullptr;
  }

  LOG_F(INFO, "RenderScene: OnAttached; input_system={} engine={}",
    static_cast<const void*>(app_.input_system.get()),
    static_cast<const void*>(engine.get()));

  // Create Pipeline
  pipeline_ = std::make_unique<renderer::ForwardPipeline>(
    observer_ptr { app_.engine.get() });

  auto shell = std::make_unique<DemoShell>();
  DemoShellConfig shell_config;
  shell_config.engine = observer_ptr { engine.get() };
  const auto demo_root
    = std::filesystem::path(std::source_location::current().file_name())
        .parent_path();
  shell_config.content_roots
    = { .content_root = demo_root.parent_path() / "Content",
        .cooked_root = demo_root / ".cooked" };
  shell_config.panel_config.content_loader = true;
  shell_config.panel_config.camera_controls = true;
  shell_config.panel_config.lighting = true;
  shell_config.panel_config.environment = true;
  shell_config.panel_config.rendering = true;
  shell_config.panel_config.post_process = true;
  shell_config.panel_config.ground_grid = true;
  shell_config.enable_camera_rig = true;
  shell_config.get_active_pipeline = [this]() {
    return observer_ptr<renderer::RenderingPipeline> { pipeline_.get() };
  };
  shell_config.on_scene_load_requested = [this](const ui::SceneEntry& entry) {
    pending_scene_load_ = SceneLoadRequest {
      .key = entry.key,
      .source_kind = entry.source.kind,
      .source_path = entry.source.path,
      .scene_name = entry.name,
    };
  };
  shell_config.on_scene_load_cancel_requested
    = [this]() { scene_load_cancel_requested_ = true; };
  shell_config.on_dump_texture_memory = [this](const std::size_t top_n) {
    if (auto* renderer = app_.renderer.get()) {
      renderer->DumpEstimatedTextureMemory(top_n);
    }
  };
  shell_config.get_last_released_scene_key
    = [this]() { return last_released_scene_key_; };
  shell_config.on_force_trim = [this]() {
    pending_source_requests_.push_back(PendingSourceRequest {
      .action = PendingSourceAction::kTrimCache,
    });
  };
  shell_config.on_clear_mounts = [this]() {
    pending_source_requests_.push_back(PendingSourceRequest {
      .action = PendingSourceAction::kClear,
    });
  };
  shell_config.on_pak_mounted = [this](const std::filesystem::path& path) {
    pending_source_requests_.push_back(PendingSourceRequest {
      .action = PendingSourceAction::kMountPak,
      .path = path,
    });
  };
  shell_config.on_loose_index_loaded
    = [this](const std::filesystem::path& path) {
        pending_source_requests_.push_back(PendingSourceRequest {
          .action = PendingSourceAction::kMountIndex,
          .path = path,
        });
      };

  if (!shell->Initialize(shell_config)) {
    LOG_F(WARNING, "RenderScene: DemoShell initialization failed");
    return nullptr;
  }

  // Create Main View ID
  main_view_id_ = GetOrCreateViewId("MainView");

  if (!app_.headless && app_window_ && app_window_->GetWindow()) {
    const auto extent = app_window_->GetWindow()->Size();
    last_viewport_ = extent;
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
  auto& shell = GetShell();
  auto& frame_context = *context;
  scene_published_this_frame_ = false;

  // Scene lifetime contract:
  // - Scene swaps/clears are applied ONLY in OnFrameStart.
  // - FrameContext stores a non-owning scene pointer for this frame.
  // - Replacing/clearing the scene in later phases (e.g. OnSceneMutation)
  //   can dangle that pointer before transform propagation.
  // Keep all ownership changes here, before frame_context.SetScene(...).
  if (pending_scene_clear_) {
    LOG_F(INFO,
      "RenderScene: Applying deferred scene clear at frame-start before "
      "FrameContext publication");
    active_scene_ = {};
    main_camera_ = {};
    scene_loader_.reset();
    active_scene_load_key_.reset();
    shell.SetScene(nullptr);
    StageFallbackScene();
    pending_scene_clear_ = false;
  }

  if (shell.HasStagedScene()) {
    CHECK_F(shell.PublishStagedScene(),
      "expected a staged scene before frame-start publish");
    scene_published_this_frame_ = true;
    active_scene_ = shell.GetActiveScene();
    main_camera_ = shell.TakePublishedMainCamera();
    LOG_F(INFO, "RenderScene: Published staged scene at frame-start");

    if (active_scene_load_key_) {
      if (const auto vm = shell.GetContentVm()) {
        vm->NotifySceneLoadCompleted(*active_scene_load_key_, true);
      }
      shell.ReapplyPostProcessSettingsToScene();
      active_scene_load_key_.reset();
    }
  }

  shell.OnFrameStart(*context);
  Base::OnFrameStart(context);

  if (!app_.headless && app_window_ && app_window_->GetWindow()) {
    last_viewport_ = app_window_->GetWindow()->Size();
  }

  const auto scene_ptr = shell.TryGetScene();
  frame_context.SetScene(observer_ptr { scene_ptr.get() });
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
      } else if (action == PendingSourceAction::kTrimCache) {
        reason = "trim cache";
      }

      if (action == PendingSourceAction::kClear) {
        ReleaseCurrentSceneAsset(reason);
        // IMPORTANT: Never clear/swap scene ownership in OnSceneMutation.
        // Defer to OnFrameStart so FrameContext scene publication remains valid
        // for the whole frame.
        pending_scene_clear_ = true;
      }

      auto asset_loader = app_.engine ? app_.engine->GetAssetLoader() : nullptr;
      if (asset_loader) {
        if (action == PendingSourceAction::kClear) {
          asset_loader->ClearMounts();
          mounted_pak_write_times_.clear();
          mounted_loose_index_write_times_.clear();
          refresh_library = true;
        } else if (action == PendingSourceAction::kTrimCache) {
          asset_loader->TrimCache();
          refresh_library = true;
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
                co_await HydrateMountedInputContextsForSource(
                  static_cast<content::AssetLoader&>(*asset_loader),
                  app_.input_system,
                  content::IAssetLoader::ContentSourceKind::kPak, normalized);
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
              co_await HydrateMountedInputContextsForSource(
                static_cast<content::AssetLoader&>(*asset_loader),
                app_.input_system,
                content::IAssetLoader::ContentSourceKind::kPak, normalized);
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
                co_await HydrateMountedInputContextsForSource(
                  static_cast<content::AssetLoader&>(*asset_loader),
                  app_.input_system,
                  content::IAssetLoader::ContentSourceKind::kLooseCooked,
                  normalized);
                mounted_loose_index_write_times_.insert_or_assign(
                  normalized, *write_time);
                refresh_library = true;
              } else if (!has_known_time && write_time.has_value()) {
                LOG_F(INFO,
                  "RenderScene: Loose cooked root '{}' mounted with unknown "
                  "index timestamp; refreshing once to bind latest index",
                  normalized.string());
                asset_loader->AddLooseCookedRoot(normalized);
                co_await HydrateMountedInputContextsForSource(
                  static_cast<content::AssetLoader&>(*asset_loader),
                  app_.input_system,
                  content::IAssetLoader::ContentSourceKind::kLooseCooked,
                  normalized);
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
              co_await HydrateMountedInputContextsForSource(
                static_cast<content::AssetLoader&>(*asset_loader),
                app_.input_system,
                content::IAssetLoader::ContentSourceKind::kLooseCooked,
                normalized);
              if (write_time.has_value()) {
                mounted_loose_index_write_times_.insert_or_assign(
                  normalized, *write_time);
              }
              refresh_library = true;
            }
          } catch (const std::exception& ex) {
            LOG_F(ERROR,
              "RenderScene: Failed to restore/mount persisted loose cooked "
              "source '{}': {}. Removing persisted entry.",
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

    auto asset_loader = app_.engine ? app_.engine->GetAssetLoader() : nullptr;
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
              co_await HydrateMountedInputContextsForSource(
                static_cast<content::AssetLoader&>(*asset_loader),
                app_.input_system,
                content::IAssetLoader::ContentSourceKind::kPak, normalized);
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
            co_await HydrateMountedInputContextsForSource(
              static_cast<content::AssetLoader&>(*asset_loader),
              app_.input_system, content::IAssetLoader::ContentSourceKind::kPak,
              normalized);
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
              co_await HydrateMountedInputContextsForSource(
                static_cast<content::AssetLoader&>(*asset_loader),
                app_.input_system,
                content::IAssetLoader::ContentSourceKind::kLooseCooked,
                normalized);
              mounted_loose_index_write_times_.insert_or_assign(
                normalized, *write_time);
            } else if (!has_known_time && write_time.has_value()) {
              LOG_F(INFO,
                "RenderScene: Loose cooked root '{}' mounted for scene load "
                "with unknown timestamp; refreshing once",
                normalized.string());
              asset_loader->AddLooseCookedRoot(normalized);
              co_await HydrateMountedInputContextsForSource(
                static_cast<content::AssetLoader&>(*asset_loader),
                app_.input_system,
                content::IAssetLoader::ContentSourceKind::kLooseCooked,
                normalized);
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
            co_await HydrateMountedInputContextsForSource(
              static_cast<content::AssetLoader&>(*asset_loader),
              app_.input_system,
              content::IAssetLoader::ContentSourceKind::kLooseCooked,
              normalized);
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
          observer_ptr { app_.engine.get() }, app_.input_system,
          observer_ptr { &app_.engine->GetScriptCompilationService() },
          PathFinder(std::make_shared<const PathFinderConfig>(
                       app_.engine->GetEngineConfig().path_finder_config),
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

  if (scene_loader_ && !shell.HasStagedScene() && !pending_physics_sidecar_) {
    if (scene_loader_->IsReady()) {
      auto loader = scene_loader_;
      auto swap = loader->GetResult();
      LOG_F(INFO, "RenderScene: Building staged scene (scene_key={})",
        data::to_string(swap.scene_key));

      const bool same_scene_key = current_scene_key_.has_value()
        && swap.scene_key == *current_scene_key_;
      if (!same_scene_key) {
        ReleaseCurrentSceneAsset("scene swap");
      }

      if (swap.asset && loader) {
        DCHECK_F(!shell.HasStagedScene(),
          "scene build path expects no pre-existing staged scene");
        shell.StageScene(
          std::make_unique<scene::Scene>("RenderScene", kSceneInitialCapacity));
        auto staged_scene = shell.GetStagedScene();
        CHECK_NOTNULL_F(staged_scene.get(),
          "staged scene must be available immediately after StageScene");
        scene::SceneNode staged_main_camera;
        try {
          staged_main_camera
            = co_await loader->BuildSceneAsync(*staged_scene, *swap.asset);
          pending_physics_sidecar_ = swap.physics_asset;
        } catch (const std::exception& ex) {
          shell.DiscardStagedScene();
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
          shell.DiscardStagedScene();
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

        shell.SetStagedMainCamera(std::move(staged_main_camera));
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
    } else if (scene_loader_->IsConsumed()) {
      if (scene_loader_->Tick()) {
        scene_loader_.reset();
      }
    }
  }

  if (pending_physics_sidecar_) {
    const bool can_hydrate_now = active_scene_.IsValid()
      && !shell.HasStagedScene() && !scene_published_this_frame_
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

  if (!active_scene_.IsValid() && !shell.HasStagedScene()) {
    StageFallbackScene();
  }

  co_await Base::OnSceneMutation(context);
  co_return;
}

auto MainModule::ReleaseCurrentSceneAsset(const char* reason) -> void
{
  // Drop active scene pin before releasing cache ownership for this scene key.
  active_scene_asset_pin_.reset();

  if (!current_scene_key_.has_value()) {
    return;
  }

  auto asset_loader = app_.engine ? app_.engine->GetAssetLoader() : nullptr;
  if (!asset_loader) {
    last_released_scene_key_ = current_scene_key_;
    current_scene_key_.reset();
    return;
  }

  LOG_F(INFO, "RenderScene: Releasing scene asset (reason={} key={})", reason,
    data::to_string(*current_scene_key_));
  last_released_scene_key_ = current_scene_key_;
  (void)asset_loader->ReleaseAsset(*current_scene_key_);
  current_scene_key_.reset();
}

auto MainModule::UpdateComposition(engine::FrameContext& context,
  std::vector<renderer::CompositionView>& views) -> void
{
  auto& shell = GetShell();
  if (!main_camera_.IsAlive()) {
    return;
  }

  View view {};
  if (app_window_ && app_window_->GetWindow()) {
    const auto extent = app_window_->GetWindow()->Size();
    view.viewport = ViewPort {
      .top_left_x = 0.0F,
      .top_left_y = 0.0F,
      .width = static_cast<float>(extent.width),
      .height = static_cast<float>(extent.height),
      .min_depth = 0.0F,
      .max_depth = 1.0F,
    };
  }

  // Create the main scene view intent
  auto main_comp
    = renderer::CompositionView::ForScene(main_view_id_, view, main_camera_);
  main_comp.with_atmosphere = true;
  shell.OnMainViewReady(context, main_comp);
  views.push_back(std::move(main_comp));

  // Also render our tools layer
  const auto imgui_view_id = GetOrCreateViewId("ImGuiView");
  views.push_back(renderer::CompositionView::ForImGui(
    imgui_view_id, view, [](graphics::CommandRecorder&) { }));
}

auto MainModule::ClearSceneRuntime(const char* /*reason*/) -> void
{
  auto& shell = GetShell();
  active_scene_ = {};
  main_camera_ = {};
  scene_loader_.reset();
  pending_physics_sidecar_.reset();
  active_scene_load_key_.reset();
  shell.SetScene(nullptr);
}

auto MainModule::StageFallbackScene() -> void
{
  auto& shell = GetShell();
  if (shell.HasStagedScene()) {
    return;
  }

  LOG_F(INFO, "RenderScene: Staging fallback default scene");
  shell.StageScene(
    std::make_unique<scene::Scene>("RenderScene", kSceneInitialCapacity));
  auto staged_scene = shell.GetStagedScene();
  CHECK_NOTNULL_F(staged_scene.get(),
    "fallback staged scene must be available after StageScene");
  auto camera_node = staged_scene->CreateNode("MainCamera");
  auto camera = std::make_unique<scene::PerspectiveCamera>();
  const bool attached = camera_node.AttachCamera(std::move(camera));
  CHECK_F(attached, "Failed to attach PerspectiveCamera to MainCamera");
  auto tf = camera_node.GetTransform();
  tf.SetLocalPosition(Vec3 { 0.0F, -6.0F, 3.0F });
  tf.SetLocalRotation(glm::quat(glm::radians(Vec3 { -20.0F, 0.0F, 0.0F })));
  shell.SetStagedMainCamera(std::move(camera_node));
}

auto MainModule::ClearBackbufferReferences() -> void
{
  if (pipeline_) {
    pipeline_->ClearBackbufferReferences();
  }
}

auto MainModule::OnGameplay(observer_ptr<engine::FrameContext> /*context*/)
  -> co::Co<>
{
  if (!logged_gameplay_tick_) {
    logged_gameplay_tick_ = true;
    LOG_F(INFO, "RenderScene: OnGameplay is running");
  }

  co_return;
}

auto MainModule::OnInput(observer_ptr<engine::FrameContext> context) -> co::Co<>
{
  auto& shell = GetShell();

  // Input edges are finalized during kInput earlier in the frame (mirrors the
  // InputSystem example). Apply camera controls here so WASD/Shift/Space and
  // mouse deltas are visible in the same frame without stomping camera
  // transforms written later by gameplay scripts.
  shell.Update(context->GetGameDeltaTime());

  co_return;
}

auto MainModule::OnGuiUpdate(observer_ptr<engine::FrameContext> context)
  -> co::Co<>
{
  DCHECK_NOTNULL_F(app_window_);
  if (!app_window_->GetWindow()) {
    co_return;
  }

  auto imgui_module_ref = app_.engine->GetModule<engine::imgui::ImGuiModule>();
  if (!imgui_module_ref) {
    co_return;
  }

  auto& imgui_module = imgui_module_ref->get();
  if (!imgui_module.IsWitinFrameScope()) {
    co_return;
  }

  auto* imgui_context = imgui_module.GetImGuiContext();
  if (imgui_context == nullptr) {
    co_return;
  }
  ImGui::SetCurrentContext(imgui_context);

  auto& shell = GetShell();
  shell.Draw(context);

  co_return;
}

auto MainModule::OnPreRender(observer_ptr<engine::FrameContext> context)
  -> co::Co<>
{
  DCHECK_NOTNULL_F(app_window_);

  if (auto imgui_module_ref
    = app_.engine->GetModule<engine::imgui::ImGuiModule>()) {
    auto& imgui_module = imgui_module_ref->get();
    if (auto* imgui_context = imgui_module.GetImGuiContext()) {
      ImGui::SetCurrentContext(imgui_context);
    }
  }

  co_await Base::OnPreRender(context);
}

} // namespace oxygen::examples::render_scene
