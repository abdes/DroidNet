//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <filesystem>
#include <source_location>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Base/StringUtils.h>
#include <Oxygen/Content/IAssetLoader.h>
#include <Oxygen/Core/FrameContext.h>
#include <Oxygen/Data/AssetKey.h>
#include <Oxygen/Engine/AsyncEngine.h>
#include <Oxygen/ImGui/ImGuiModule.h>
#include <Oxygen/Renderer/Internal/SkyAtmosphereLutManager.h>
#include <Oxygen/Renderer/Passes/LightCullingPass.h>
#include <Oxygen/Renderer/Passes/ShaderPass.h>
#include <Oxygen/Renderer/Renderer.h>
#include <Oxygen/Scene/Camera/Perspective.h>
#include <Oxygen/Scene/Scene.h>

#include "DemoShell/DemoShell.h"
#include "DemoShell/Runtime/CompositionView.h"
#include "DemoShell/Runtime/DemoAppContext.h"
#include "DemoShell/Runtime/ForwardPipeline.h"
#include "DemoShell/Services/SceneLoaderService.h"
#include "DemoShell/UI/ContentVm.h"
#include "RenderScene/MainModule.h"

using oxygen::scene::SceneNodeFlags;

namespace oxygen::examples::render_scene {

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

auto MainModule::OnAttachedImpl(observer_ptr<AsyncEngine> engine) noexcept
  -> std::unique_ptr<DemoShell>
{
  if (!engine) {
    return nullptr;
  }

  LOG_F(INFO, "RenderScene: OnAttached; input_system={} engine={}",
    static_cast<const void*>(app_.input_system.get()),
    static_cast<const void*>(engine.get()));

  // Create Pipeline
  pipeline_
    = std::make_unique<ForwardPipeline>(observer_ptr { app_.engine.get() });

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
  shell_config.enable_camera_rig = true;
  shell_config.get_active_pipeline
    = [this]() -> observer_ptr<RenderingPipeline> {
    return observer_ptr { pipeline_.get() };
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
  shell_config.on_force_trim
    = [this]() { pending_source_action_ = PendingSourceAction::kTrimCache; };
  shell_config.on_pak_mounted = [this](const std::filesystem::path& path) {
    pending_source_action_ = PendingSourceAction::kMountPak;
    pending_path_ = path;
  };
  shell_config.on_loose_index_loaded
    = [this](const std::filesystem::path& path) {
        pending_source_action_ = PendingSourceAction::kMountIndex;
        pending_path_ = path;
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
  Base::OnShutdown();
}

auto MainModule::OnFrameStart(observer_ptr<engine::FrameContext> context)
  -> void
{
  DCHECK_NOTNULL_F(context);
  auto& shell = GetShell();
  shell.OnFrameStart(*context);
  Base::OnFrameStart(context);
  auto& frame_context = *context;

  if (!app_.headless && app_window_ && app_window_->GetWindow()) {
    last_viewport_ = app_window_->GetWindow()->Size();
  }

  // 1. Process deferred lifecycle actions before anything else this frame.
  if (pending_source_action_ != PendingSourceAction::kNone) {
    const char* reason = "source change";
    if (pending_source_action_ == PendingSourceAction::kMountPak) {
      reason = "pak mounted";
    } else if (pending_source_action_ == PendingSourceAction::kMountIndex) {
      reason = "loose cooked root";
    } else if (pending_source_action_ == PendingSourceAction::kClear) {
      reason = "force trim";
    } else if (pending_source_action_ == PendingSourceAction::kTrimCache) {
      reason = "trim cache";
    }

    if (pending_source_action_ == PendingSourceAction::kClear) {
      ReleaseCurrentSceneAsset(reason);
      ClearSceneRuntime(reason);
    }

    auto asset_loader = app_.engine ? app_.engine->GetAssetLoader() : nullptr;
    if (asset_loader) {
      if (pending_source_action_ == PendingSourceAction::kClear
        || pending_source_action_ == PendingSourceAction::kTrimCache) {
        asset_loader->TrimCache();
      } else if (pending_source_action_ == PendingSourceAction::kMountPak) {
        asset_loader->AddPakFile(pending_path_);
      } else if (pending_source_action_ == PendingSourceAction::kMountIndex) {
        std::error_code ec;
        auto root = pending_path_.parent_path();
        auto normalized = std::filesystem::weakly_canonical(root, ec);
        if (ec) {
          normalized = root.lexically_normal();
        }

        const auto already_mounted = std::any_of(mounted_loose_roots_.begin(),
          mounted_loose_roots_.end(),
          [&normalized](const std::filesystem::path& existing) {
            return existing == normalized;
          });

        if (already_mounted) {
          LOG_F(INFO,
            "RenderScene: Loose cooked root already mounted; skipping "
            "refresh for '{}'",
            normalized.string());
        } else {
          asset_loader->AddLooseCookedRoot(root);
          mounted_loose_roots_.push_back(std::move(normalized));
        }
      }
    }

    if (const auto vm = shell.GetContentVm()) {
      vm->RefreshLibrary();
    }

    pending_source_action_ = PendingSourceAction::kNone;
    pending_path_.clear();
  }

  // 2. Process pending scene loads
  if (scene_load_cancel_requested_) {
    LOG_F(INFO, "RenderScene: Scene load cancellation requested");
    scene_load_cancel_requested_ = false;
    pending_scene_load_.reset();
    scene_loader_.reset();
    active_scene_load_key_.reset();
  }

  if (pending_scene_load_) {
    const auto request = *pending_scene_load_;
    pending_scene_load_.reset();

    auto asset_loader = app_.engine ? app_.engine->GetAssetLoader() : nullptr;
    if (asset_loader) {
      try {
        if (request.source_kind == ui::SceneSourceKind::kPak) {
          asset_loader->AddPakFile(request.source_path);
          LOG_F(INFO,
            "RenderScene: Ensured PAK source '{}' for scene load '{}'",
            request.source_path.string(), request.scene_name);
        } else if (request.source_kind == ui::SceneSourceKind::kLooseIndex) {
          const auto root = request.source_path.parent_path();
          std::error_code ec;
          auto normalized = std::filesystem::weakly_canonical(root, ec);
          if (ec) {
            normalized = root.lexically_normal();
          }

          const auto already_mounted = std::any_of(mounted_loose_roots_.begin(),
            mounted_loose_roots_.end(),
            [&normalized](const std::filesystem::path& existing) {
              return existing == normalized;
            });

          if (already_mounted) {
            LOG_F(INFO,
              "RenderScene: Loose cooked root already mounted; skipping "
              "refresh for '{}'",
              normalized.string());
          } else {
            asset_loader->AddLooseCookedRoot(root);
            mounted_loose_roots_.push_back(std::move(normalized));
          }
          LOG_F(INFO,
            "RenderScene: Ensured loose cooked root '{}' for scene load '{}'",
            root.string(), request.scene_name);
        }
      } catch (const std::exception& ex) {
        LOG_F(ERROR,
          "RenderScene: Failed to remount source for scene load (scene='{}' "
          "error='{}')",
          request.scene_name, ex.what());
        if (const auto vm = shell.GetContentVm()) {
          vm->NotifySceneLoadCompleted(request.key, false);
        }
        return;
      }

      scene_loader_
        = std::make_shared<SceneLoaderService>(*asset_loader, last_viewport_);
      active_scene_load_key_ = request.key;
      scene_loader_->StartLoad(request.key);
      LOG_F(INFO,
        "RenderScene: Started async scene load (scene_key={} scene='{}')",
        data::to_string(request.key), request.scene_name);
    } else {
      LOG_F(ERROR, "AssetLoader unavailable");
    }
  }

  // 3. Process async scene loading results
  if (scene_loader_) {
    if (scene_loader_->IsReady()) {
      auto loader = scene_loader_;
      auto swap = scene_loader_->GetResult();
      LOG_F(INFO, "RenderScene: Applying staged scene swap (scene_key={})",
        data::to_string(swap.scene_key));
      const bool same_scene_key = current_scene_key_.has_value()
        && swap.scene_key == *current_scene_key_;
      if (!same_scene_key) {
        ReleaseCurrentSceneAsset("scene swap");
      }
      ClearSceneRuntime("scene swap");

      {
        auto scene = std::make_unique<scene::Scene>("RenderScene");
        active_scene_ = shell.SetScene(std::move(scene));
        const auto scene_ptr = shell.TryGetScene();
        if (scene_ptr && swap.asset && loader) {
          auto active_camera = loader->BuildScene(*scene_ptr, *swap.asset);
          main_camera_ = std::move(active_camera);
        } else {
          LOG_F(ERROR, "RenderScene: Scene swap missing asset or scene");
        }
      }
      if (const auto vm = shell.GetContentVm()) {
        vm->NotifySceneLoadCompleted(swap.scene_key, true);
      }
      active_scene_load_key_.reset();
      current_scene_key_ = swap.scene_key;

      scene_loader_ = std::move(loader);
      if (scene_loader_) {
        scene_loader_->MarkConsumed();
      }
    } else if (scene_loader_->IsFailed()) {
      LOG_F(ERROR, "RenderScene: Scene loading failed");
      if (const auto vm = shell.GetContentVm()) {
        if (active_scene_load_key_.has_value()) {
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

  if (!active_scene_.IsValid()) {
    auto scene = std::make_unique<scene::Scene>("RenderScene");
    active_scene_ = shell.SetScene(std::move(scene));
  }

  if (!main_camera_.IsAlive()) {
    if (const auto scene_ptr = shell.TryGetScene()) {
      main_camera_ = scene_ptr->CreateNode("MainCamera");
      auto camera = std::make_unique<scene::PerspectiveCamera>();
      const bool attached = main_camera_.AttachCamera(std::move(camera));
      CHECK_F(attached, "Failed to attach PerspectiveCamera to MainCamera");
    }
  }

  const auto scene_ptr = shell.TryGetScene();

  frame_context.SetScene(observer_ptr { scene_ptr.get() });
}

auto MainModule::ReleaseCurrentSceneAsset(const char* reason) -> void
{
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

auto MainModule::UpdateComposition(
  engine::FrameContext& context, std::vector<CompositionView>& views) -> void
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
  auto main_comp = CompositionView::ForScene(main_view_id_, view, main_camera_);
  shell.OnMainViewReady(context, main_comp);
  views.push_back(std::move(main_comp));

  // Also render our tools layer
  const auto imgui_view_id = GetOrCreateViewId("ImGuiView");
  views.push_back(CompositionView::ForImGui(
    imgui_view_id, view, [](graphics::CommandRecorder&) { }));
}

auto MainModule::ClearSceneRuntime(const char* /*reason*/) -> void
{
  auto& shell = GetShell();
  active_scene_ = {};
  main_camera_ = {};
  scene_loader_.reset();
  shell.SetScene(nullptr);
}

auto MainModule::ClearBackbufferReferences() -> void
{
  if (pipeline_) {
    pipeline_->ClearBackbufferReferences();
  }
}

auto MainModule::OnGameplay(observer_ptr<engine::FrameContext> context)
  -> co::Co<>
{
  auto& shell = GetShell();
  if (!logged_gameplay_tick_) {
    logged_gameplay_tick_ = true;
    LOG_F(INFO, "RenderScene: OnGameplay is running");
  }

  // Input edges are finalized during kInput earlier in the frame (mirrors the
  // InputSystem example). Apply camera controls here so WASD/Shift/Space and
  // mouse deltas are visible in the same frame.
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

  auto& shell = GetShell();
  shell.Draw(context);
  co_return;
}

auto MainModule::OnPreRender(observer_ptr<engine::FrameContext> context)
  -> co::Co<>
{
  DCHECK_NOTNULL_F(app_window_);

  if (auto imgui_module_ref = app_.engine->GetModule<imgui::ImGuiModule>()) {
    auto& imgui_module = imgui_module_ref->get();
    if (auto* imgui_context = imgui_module.GetImGuiContext()) {
      ImGui::SetCurrentContext(imgui_context);
    }
  }

  co_await Base::OnPreRender(context);
}

} // namespace examples::render_scene
