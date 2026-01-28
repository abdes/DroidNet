//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <functional>
#include <numbers>
#include <source_location>
#include <string_view>
#include <type_traits>
#include <unordered_set>
#include <utility>
#include <vector>

#include <imgui.h>

#include <glm/ext/matrix_transform.hpp>
#include <glm/geometric.hpp>
#include <glm/gtc/constants.hpp>
#include <glm/gtc/quaternion.hpp>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Base/StringUtils.h>
#include <Oxygen/Content/IAssetLoader.h>
#include <Oxygen/Content/LooseCookedInspection.h>
#include <Oxygen/Content/PakFile.h>
#include <Oxygen/Core/Constants.h>
#include <Oxygen/Core/Types/ViewPort.h>
#include <Oxygen/Data/AssetKey.h>
#include <Oxygen/Data/AssetType.h>
#include <Oxygen/Data/SceneAsset.h>
#include <Oxygen/Engine/AsyncEngine.h>
#include <Oxygen/ImGui/ImGuiModule.h>
#include <Oxygen/Renderer/Internal/SkyAtmosphereLutManager.h>
#include <Oxygen/Renderer/Renderer.h>
#include <Oxygen/Scene/Camera/Orthographic.h>
#include <Oxygen/Scene/Camera/Perspective.h>
#include <Oxygen/Scene/Environment/Fog.h>
#include <Oxygen/Scene/Environment/PostProcessVolume.h>
#include <Oxygen/Scene/Environment/SceneEnvironment.h>
#include <Oxygen/Scene/Environment/SkyAtmosphere.h>
#include <Oxygen/Scene/Environment/SkyLight.h>
#include <Oxygen/Scene/Environment/SkySphere.h>
#include <Oxygen/Scene/Environment/Sun.h>
#include <Oxygen/Scene/Environment/VolumetricClouds.h>
#include <Oxygen/Scene/Light/DirectionalLight.h>
#include <Oxygen/Scene/Light/PointLight.h>
#include <Oxygen/Scene/Light/SpotLight.h>
#include <Oxygen/Scene/Types/Flags.h>

#include "DemoShell/DemoShell.h"
#include "DemoShell/Services/SceneLoaderService.h"
#include "RenderScene/MainModule.h"

using oxygen::scene::SceneNodeFlags;

namespace oxygen::examples::render_scene {

MainModule::MainModule(const oxygen::examples::DemoAppContext& app)
  : Base(app)
{
  cooked_root_
    = std::filesystem::path(std::source_location::current().file_name())
        .parent_path()
    / ".cooked";
}

MainModule::~MainModule() = default;

auto MainModule::BuildDefaultWindowProperties() const
  -> platform::window::Properties
{
  platform::window::Properties p("Oxygen Example");
  p.extent = { .width = 2560U, .height = 1400 };
  p.flags = { .hidden = false,
    .always_on_top = false,
    .full_screen = app_.fullscreen,
    .maximized = false,
    .minimized = false,
    .resizable = true,
    .borderless = false };
  return p;
}

auto MainModule::OnAttached(
  oxygen::observer_ptr<oxygen::AsyncEngine> engine) noexcept -> bool
{
  if (!engine) {
    return false;
  }

  if (!Base::OnAttached(engine)) {
    return false;
  }

  LOG_F(INFO, "RenderScene: OnAttached; input_system={} engine={}",
    static_cast<const void*>(app_.input_system.get()),
    static_cast<const void*>(engine.get()));

  shell_ = std::make_unique<DemoShell>();
  file_browser_service_ = std::make_unique<FileBrowserService>();
  file_browser_service_->ConfigureContentRoots(ContentRootConfig {
    .cooked_root = cooked_root_,
  });
  DemoShellConfig shell_config;
  shell_config.input_system = observer_ptr { app_.input_system.get() };
  shell_config.scene = scene_;
  shell_config.cooked_root = cooked_root_;
  shell_config.file_browser_service
    = observer_ptr { file_browser_service_.get() };
  shell_config.skybox_service = observer_ptr { skybox_service_.get() };
  shell_config.get_renderer
    = [this]() { return observer_ptr { ResolveRenderer() }; };
  shell_config.get_light_culling_debug_config = [this]() {
    ui::LightCullingDebugConfig debug_config;
    if (auto render_graph = GetRenderGraph()) {
      debug_config.shader_pass_config
        = observer_ptr { render_graph->GetShaderPassConfig().get() };
      debug_config.light_culling_pass_config
        = observer_ptr { render_graph->GetLightCullingPassConfig().get() };
    }
    return debug_config;
  };
  shell_config.on_scene_load_requested = [this](const data::AssetKey& key) {
    pending_scene_key_ = key;
    pending_load_scene_ = true;
  };
  shell_config.on_dump_texture_memory = [this](const std::size_t top_n) {
    if (auto* renderer = ResolveRenderer()) {
      renderer->DumpEstimatedTextureMemory(top_n);
    }
  };
  shell_config.get_last_released_scene_key
    = [this]() { return last_released_scene_key_; };
  shell_config.on_force_trim = [this]() {
    ReleaseCurrentSceneAsset("force trim");
    ClearSceneRuntime("force trim");
  };
  shell_config.on_pak_mounted = [this](const std::filesystem::path& path) {
    auto asset_loader = app_.engine ? app_.engine->GetAssetLoader() : nullptr;
    if (asset_loader) {
      ReleaseCurrentSceneAsset("pak mounted");
      ClearSceneRuntime("pak mounted");
      asset_loader->ClearMounts();
      asset_loader->AddPakFile(path);
    }
  };
  shell_config.on_loose_index_loaded = [this](
                                         const std::filesystem::path& path) {
    auto asset_loader = app_.engine ? app_.engine->GetAssetLoader() : nullptr;
    if (asset_loader) {
      ReleaseCurrentSceneAsset("loose cooked root");
      ClearSceneRuntime("loose cooked root");
      asset_loader->ClearMounts();
      asset_loader->AddLooseCookedRoot(path.parent_path());
    }
  };

  if (!shell_->Initialize(shell_config)) {
    LOG_F(WARNING, "RenderScene: DemoShell initialization failed");
    return false;
  }

  LOG_F(INFO, "RenderScene: DemoShell initialized");
  return true;
}

void MainModule::OnShutdown() noexcept
{
  if (shell_) {
    shell_->CancelContentImport();
  }
  ReleaseCurrentSceneAsset("module shutdown");
  ClearSceneRuntime("module shutdown");
  Base::OnShutdown();
}

auto MainModule::OnFrameStart(oxygen::engine::FrameContext& context) -> void
{
  Base::OnFrameStart(context);
}

auto MainModule::OnExampleFrameStart(engine::FrameContext& context) -> void
{
  if (scene_loader_) {
    if (scene_loader_->IsReady()) {
      auto loader = scene_loader_;
      auto swap = scene_loader_->GetResult();
      LOG_F(INFO, "RenderScene: Applying staged scene swap (scene_key={})",
        oxygen::data::to_string(swap.scene_key));
      ReleaseCurrentSceneAsset("scene swap");
      ClearSceneRuntime("scene swap");

      scene_ = std::move(swap.scene);
      if (shell_) {
        shell_->UpdateScene(scene_);
        shell_->SetActiveCamera(std::move(swap.active_camera));
      }
      current_scene_key_ = swap.scene_key;
      if (shell_ && shell_->GetCameraLifecycle().GetActiveCamera().IsAlive()) {
        shell_->GetCameraLifecycle().CaptureInitialPose();
        shell_->GetCameraLifecycle().EnsureFlyCameraFacingScene();
      }
      registered_view_camera_ = scene::NodeHandle();
      scene_loader_ = std::move(loader);
      if (scene_loader_) {
        scene_loader_->MarkConsumed();
      }
    } else if (scene_loader_->IsFailed()) {
      LOG_F(ERROR, "RenderScene: Scene loading failed");
      scene_loader_.reset();
    } else if (scene_loader_->IsConsumed()) {
      if (scene_loader_->Tick()) {
        scene_loader_.reset();
      }
    }
  }

  if (!scene_) {
    scene_ = std::make_shared<scene::Scene>("RenderScene");
    if (shell_) {
      shell_->UpdateScene(scene_);
    }
  }

  // Keep the skybox helper bound to the current scene.
  if (skybox_service_scene_ != scene_.get()) {
    auto asset_loader = app_.engine ? app_.engine->GetAssetLoader() : nullptr;
    if (asset_loader) {
      skybox_service_ = std::make_unique<SkyboxService>(
        observer_ptr { asset_loader.get() }, scene_);
      skybox_service_scene_ = scene_.get();
    } else {
      skybox_service_.reset();
      skybox_service_scene_ = nullptr;
    }
    if (shell_) {
      shell_->SetSkyboxService(observer_ptr { skybox_service_.get() });
    }
  }
  context.SetScene(observer_ptr { scene_.get() });
}

auto MainModule::OnSceneMutation(engine::FrameContext& context) -> co::Co<>
{
  DCHECK_NOTNULL_F(app_window_);
  DCHECK_NOTNULL_F(scene_);

  UpdateFrameContext(context, [this, &context](int w, int h) {
    last_viewport_w_ = w;
    last_viewport_h_ = h;
    if (shell_) {
      auto& camera_lifecycle = shell_->GetCameraLifecycle();
      camera_lifecycle.EnsureViewport(w, h);
      camera_lifecycle.ApplyPendingSync();
      camera_lifecycle.ApplyPendingReset();
    }
  });
  if (!app_window_->GetWindow()) {
    co_return;
  }

  // Panel updates happen here before scene loading
  if (shell_) {
    shell_->Update(time::CanonicalDuration {});
  }

  if (pending_load_scene_) {
    pending_load_scene_ = false;

    if (pending_scene_key_) {
      ReleaseCurrentSceneAsset("scene load request");
      ClearSceneRuntime("scene load request");
      auto asset_loader = app_.engine ? app_.engine->GetAssetLoader() : nullptr;
      if (asset_loader) {
        scene_loader_ = std::make_shared<SceneLoaderService>(
          *asset_loader, last_viewport_w_, last_viewport_h_);
        scene_loader_->StartLoad(*pending_scene_key_);
        LOG_F(INFO, "RenderScene: Started async scene load (scene_key={})",
          oxygen::data::to_string(*pending_scene_key_));
      } else {
        LOG_F(ERROR, "AssetLoader unavailable");
      }
    }
  }

  co_return;
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
    oxygen::data::to_string(*current_scene_key_));
  last_released_scene_key_ = current_scene_key_;
  (void)asset_loader->ReleaseAsset(*current_scene_key_);
  current_scene_key_.reset();
}

auto MainModule::ClearSceneRuntime(const char* reason) -> void
{
  UnregisterViewForRendering(reason);
  scene_.reset();
  scene_loader_.reset();
  if (shell_) {
    shell_->UpdateScene(nullptr);
    shell_->GetCameraLifecycle().Clear();
    shell_->SetSkyboxService(observer_ptr<SkyboxService> { nullptr });
  }
  registered_view_camera_ = scene::NodeHandle();
  skybox_service_.reset();
  skybox_service_scene_ = nullptr;
}

auto MainModule::OnGameplay(engine::FrameContext& context) -> co::Co<>
{
  if (!logged_gameplay_tick_) {
    logged_gameplay_tick_ = true;
    LOG_F(INFO, "RenderScene: OnGameplay is running");
  }

  // Input edges are finalized during kInput earlier in the frame (mirrors the
  // InputSystem example). Apply camera controls here so WASD/Shift/Space and
  // mouse deltas are visible in the same frame.
  if (shell_) {
    shell_->Update(context.GetGameDeltaTime());
  }

  co_return;
}

auto MainModule::EnsureViewCameraRegistered() -> void
{
  if (!shell_) {
    return;
  }
  auto& active_camera = shell_->GetCameraLifecycle().GetActiveCamera();
  if (!active_camera.IsAlive()) {
    return;
  }

  const auto camera_handle = active_camera.GetHandle();
  if (registered_view_camera_ != camera_handle) {
    registered_view_camera_ = camera_handle;
    UnregisterViewForRendering("camera changed");
    LOG_F(INFO, "RenderScene: Active camera changed; re-registering view");
  }

  RegisterViewForRendering(active_camera);
}

auto MainModule::OnGuiUpdate(engine::FrameContext& context) -> co::Co<>
{
  DCHECK_NOTNULL_F(app_window_);
  if (!app_window_->GetWindow()) {
    co_return;
  }
  auto imgui_module_ref
    = app_.engine ? app_.engine->GetModule<imgui::ImGuiModule>() : std::nullopt;

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

  if (shell_) {
    shell_->Draw();
  }
  co_return;
}

auto MainModule::OnPreRender(engine::FrameContext& context) -> co::Co<>
{
  DCHECK_NOTNULL_F(app_window_);

  if (auto imgui_module_ref = app_.engine->GetModule<imgui::ImGuiModule>()) {
    auto& imgui_module = imgui_module_ref->get();
    if (auto* imgui_context = imgui_module.GetImGuiContext()) {
      ImGui::SetCurrentContext(imgui_context);
    }
  }

  ApplyRenderModeFromPanel();

  if (auto rg = GetRenderGraph(); rg) {
    rg->SetupRenderPasses();
  }

  EnsureViewCameraRegistered();
  co_return;
}

auto MainModule::OnCompositing(engine::FrameContext& context) -> co::Co<>
{
  MarkSurfacePresentable(context);
  co_return;
}

auto MainModule::OnFrameEnd(engine::FrameContext& context) -> void
{
  Base::OnFrameEnd(context);
}

auto MainModule::ApplyRenderModeFromPanel() -> void
{
  if (!shell_) {
    return;
  }
  if (auto render_graph = GetRenderGraph()) {
    auto shader_pass_config = render_graph->GetShaderPassConfig();
    auto transparent_pass_config = render_graph->GetTransparentPassConfig();
    if (!shader_pass_config || !transparent_pass_config) {
      return;
    }

    using graphics::FillMode;
    const auto view_mode = shell_->GetRenderingViewMode();
    const FillMode mode = (view_mode == ui::RenderingViewMode::kWireframe)
      ? FillMode::kWireFrame
      : FillMode::kSolid;
    render_graph->SetWireframeEnabled(mode == FillMode::kWireFrame);
    shader_pass_config->fill_mode = mode;
    transparent_pass_config->fill_mode = mode;

    const bool force_clear = (mode == FillMode::kWireFrame);
    shader_pass_config->clear_color_target = true;
    shader_pass_config->auto_skip_clear_when_sky_pass_present = !force_clear;
  }
}

} // namespace oxygen::examples::render_scene
