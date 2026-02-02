//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <filesystem>
#include <memory>
#include <source_location>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <imgui.h>

#include <Oxygen/Base/Macros.h>
#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Content/IAssetLoader.h>
#include <Oxygen/Engine/AsyncEngine.h>
#include <Oxygen/ImGui/ImGuiModule.h>
#include <Oxygen/Renderer/Renderer.h>

#include "DemoShell/Runtime/DemoAppContext.h"
#include "DemoShell/Runtime/ForwardPipeline.h"
#include "DemoShell/Runtime/SceneView.h"
#include "DemoShell/Services/FileBrowserService.h"
#include "DemoShell/Services/SkyboxService.h"
#include "DemoShell/UI/DemoShellUi.h"
#include "DemoShell/UI/RenderingVm.h"
#include "Oxygen/Scene/Camera/Perspective.h"
#include "TexturedCube/MainModule.h"

namespace oxygen::examples::textured_cube {

MainModule::MainModule(const DemoAppContext& app)
  : Base(app)
{
  DCHECK_NOTNULL_F(app_.platform);
  DCHECK_F(!app_.gfx_weak.expired());
  cooked_root_
    = std::filesystem::path(std::source_location::current().file_name())
        .parent_path()
    / ".cooked";

  content_root_
    = std::filesystem::path(std::source_location::current().file_name())
        .parent_path()
        .parent_path()
    / "Content";
}

auto MainModule::BuildDefaultWindowProperties() const
  -> platform::window::Properties
{
  platform::window::Properties p("Textured Cube (Demo Shell)");
  p.extent = { .width = 2560U, .height = 1400U };
  p.flags = { .hidden = false,
    .always_on_top = false,
    .full_screen = app_.fullscreen,
    .maximized = false,
    .minimized = false,
    .resizable = true,
    .borderless = false };
  return p;
}

auto MainModule::ClearBackbufferReferences() -> void
{
  if (pipeline_) {
    pipeline_->ClearBackbufferReferences();
  }
}

auto MainModule::OnAttached(observer_ptr<AsyncEngine> engine) noexcept -> bool
{
  if (!engine) {
    return false;
  }

  if (!Base::OnAttached(engine)) {
    return false;
  }

  // Create Pipeline
  auto fw_pipeline
    = std::make_unique<ForwardPipeline>(observer_ptr { app_.engine.get() });

  pipeline_ = std::move(fw_pipeline);

  shell_ = std::make_unique<DemoShell>();
  DemoShellConfig shell_config;
  shell_config.engine = engine;
  shell_config.content_roots = {
    .content_root = content_root_,
    .cooked_root = cooked_root_,
  };

  shell_config.panel_config.content_loader
    = false; // We use custom texture loader
  shell_config.panel_config.camera_controls = true;
  shell_config.panel_config.lighting = true;
  shell_config.panel_config.environment = true;
  shell_config.panel_config.rendering = true;

  // Shared services wiring
  // Note: We initialize SkyboxService later when Scene is ready,
  // so we update the shell with it later.

  shell_config.get_active_pipeline
    = [this]() -> observer_ptr<RenderingPipeline> {
    return observer_ptr { pipeline_.get() };
  };

  if (!shell_->Initialize(shell_config)) {
    LOG_F(ERROR, "TexturedCube: DemoShell initialization failed");
    return false;
  }

  // Create Main View (placeholder camera, updated later)
  auto view = std::make_unique<SceneView>(scene::SceneNode {});
  main_view_
    = observer_ptr { static_cast<SceneView*>(AddView(std::move(view))) };

  LOG_F(INFO, "TexturedCube: Module initialized");
  return true;
}

auto MainModule::OnShutdown() noexcept -> void
{
  // Clear scene from shell first to ensure controlled destruction
  if (shell_) {
    shell_->SetScene(nullptr);
  }

  // Destroy setup before other services
  scene_setup_.reset();

  skybox_service_.reset();
  texture_service_.reset();

  texture_panel_.reset();
  texture_vm_.reset();

  shell_.reset();

  // Clear observers
  active_scene_ = {};
  Base::OnShutdown();
}

auto MainModule::OnFrameStart(engine::FrameContext& context) -> void
{
  Base::OnFrameStart(context);
}

auto MainModule::HandleOnFrameStart(engine::FrameContext& context) -> void
{
  if (!active_scene_.IsValid()) {
    // 1. Create Scene and transfer to Shell
    auto scene_unique = std::make_unique<scene::Scene>("TexturedCube-Scene");
    active_scene_ = shell_->SetScene(std::move(scene_unique));

    // 2. Initialize Services
    auto asset_loader = app_.engine ? app_.engine->GetAssetLoader() : nullptr;
    if (asset_loader) {
      texture_service_ = std::make_unique<TextureLoadingService>(
        observer_ptr<oxygen::content::IAssetLoader> { asset_loader.get() });
      skybox_service_ = std::make_unique<SkyboxService>(
        observer_ptr<oxygen::content::IAssetLoader> { asset_loader.get() },
        observer_ptr { active_scene_.operator->() });

      // Init VM
      texture_vm_ = std::make_unique<ui::TextureBrowserVm>(
        observer_ptr { texture_service_.get() },
        observer_ptr { &shell_->GetFileBrowserService() });
      texture_vm_->SetCubeRebuildNeeded();

      texture_vm_->SetOnSkyboxSelected(
        [this](oxygen::content::ResourceKey key) {
          if (skybox_service_) {
            skybox_service_->SetSkyboxResourceKey(key);
            SkyboxService::SkyLightParams params;
            params.intensity = 1.0F;
            params.diffuse_intensity = 1.0F;
            params.specular_intensity = 1.0F;
            skybox_service_->ApplyToScene(params);
          }
        });

      // Init Panel
      texture_panel_ = std::make_shared<ui::TextureBrowserPanel>();
      texture_panel_->Initialize(observer_ptr { texture_vm_.get() });

      shell_->RegisterPanel(texture_panel_);
      shell_->SetActivePanel("Texture Browser");

      // 3. Initialize Scene Setup
      scene_setup_ = std::make_unique<SceneSetup>(
        observer_ptr { active_scene_.operator->() }, *texture_service_,
        *skybox_service_, cooked_root_);
      scene_setup_->EnsureEnvironment({});
      scene_setup_->EnsureLighting({}, {});
      scene_setup_->EnsureCubeNode();
    }
  }

  context.SetScene(shell_->TryGetScene());
}

auto MainModule::OnSceneMutation(engine::FrameContext& context) -> co::Co<>
{
  DCHECK_NOTNULL_F(app_window_);
  DCHECK_NOTNULL_F(shell_);

  if (!app_window_->GetWindow()) {
    co_return;
  }

  if (!active_scene_.IsValid()) {
    co_return;
  }

  // Get window extent for camera setup via shell's camera lifecycle
  const auto extent = app_window_->GetWindow()->Size();
  auto& camera_lifecycle = shell_->GetCameraLifecycle();
  camera_lifecycle.EnsureViewport(extent.width, extent.height);
  camera_lifecycle.ApplyPendingSync();
  camera_lifecycle.ApplyPendingReset();

  // Update shell
  shell_->Update(context.GetGameDeltaTime());

  // Update view camera from shell's camera rig
  if (main_view_) {
    auto& active_camera = camera_lifecycle.GetActiveCamera();
    if (active_camera.IsAlive()) {
      main_view_->SetCamera(active_camera);
    }
  }

  // Ensure scene objects (idempotent)
  if (scene_setup_) {
    scene_setup_->EnsureCubeNode();
    // scene_setup_->EnsureEnvironment({}); // Managed by SkyboxService + Shell?
    // scene_setup_->EnsureLighting({}, {}); // Managed by Shell Lighting
  }

  // Update Texture State
  if (texture_vm_ && scene_setup_) {
    // Check for Rebuild
    if (texture_vm_->IsCubeRebuildNeeded()) {
      texture_vm_->ClearCubeRebuildNeeded();

      auto& sphere_state = texture_vm_->GetSphereTextureState();
      auto& cube_state = texture_vm_->GetCubeTextureState();

      auto asset_loader = app_.engine ? app_.engine->GetAssetLoader() : nullptr;
      if ((sphere_state.mode == SceneSetup::TextureIndexMode::kForcedError
            || cube_state.mode == SceneSetup::TextureIndexMode::kForcedError)
        && (forced_error_key_ == oxygen::content::ResourceKey { 0U })
        && asset_loader) {
        forced_error_key_ = asset_loader->MintSyntheticTextureKey();
      }

      auto& surface = texture_vm_->GetSurfaceState();

      auto [uv_scale, uv_offset] = texture_vm_->GetEffectiveUvTransform();

      glm::vec4 base_color { 1.0F };
      if (surface.use_constant_base_color) {
        base_color = { surface.constant_base_color_rgb, 1.0F };
      }

      // Rebuild Cube
      auto material = scene_setup_->RebuildCube(sphere_state.mode,
        sphere_state.resource_index, sphere_state.resource_key, cube_state.mode,
        cube_state.resource_index, cube_state.resource_key, forced_error_key_,
        uv_scale, uv_offset, surface.metalness, surface.roughness, base_color,
        surface.use_constant_base_color);

      // Apply overrides immediately
      auto* renderer = [this]() -> engine::Renderer* {
        if (app_.engine) {
          if (auto r = app_.engine->GetModule<engine::Renderer>())
            return &r->get();
        }
        return nullptr;
      }();
      if (renderer && material) {
        // Sticky overrides
        if (auto sphere_mat = scene_setup_->GetSphereMaterial()) {
          (void)renderer->OverrideMaterialUvTransform(
            *sphere_mat, uv_scale, uv_offset);
        }
        if (auto cube_mat = scene_setup_->GetCubeMaterial()) {
          (void)renderer->OverrideMaterialUvTransform(
            *cube_mat, uv_scale, uv_offset);
        }
      }
    } else {
      // Sticky Overrides even if not rebuilt (e.g. UV changed)
      auto* renderer = [this]() -> engine::Renderer* {
        if (app_.engine) {
          if (auto r = app_.engine->GetModule<engine::Renderer>())
            return &r->get();
        }
        return nullptr;
      }();
      if (renderer) {
        auto [uv_scale, uv_offset] = texture_vm_->GetEffectiveUvTransform();
        if (auto sphere_mat = scene_setup_->GetSphereMaterial()) {
          (void)renderer->OverrideMaterialUvTransform(
            *sphere_mat, uv_scale, uv_offset);
        }
        if (auto cube_mat = scene_setup_->GetCubeMaterial()) {
          (void)renderer->OverrideMaterialUvTransform(
            *cube_mat, uv_scale, uv_offset);
        }
      }
    }
  }

  // Delegate to pipeline
  co_await Base::OnSceneMutation(context);
}

auto MainModule::OnGameplay(engine::FrameContext& context) -> co::Co<>
{
  if (shell_) {
    shell_->Update(context.GetGameDeltaTime());
  }
  co_return;
}

auto MainModule::OnGuiUpdate(engine::FrameContext& context) -> co::Co<>
{
  // We only draw ImGui if we still have an app window open
  DCHECK_NOTNULL_F(app_window_);
  if (!app_window_->GetWindow()) {
    co_return;
  }

  if (shell_) {
    shell_->Draw(context);
  }

  co_return;
}

auto MainModule::OnPreRender(engine::FrameContext& context) -> co::Co<>
{
  DCHECK_NOTNULL_F(app_window_);

  if (shell_) {
    ApplyRenderModeFromPanel();
  }

  if (auto* fw_pipeline = static_cast<ForwardPipeline*>(pipeline_.get())) {
    if (auto shader_pass_config = fw_pipeline->GetShaderPassConfig()) {
      shader_pass_config->clear_color
        = graphics::Color { 0.08F, 0.08F, 0.10F, 1.0F };
      shader_pass_config->debug_name = "ShaderPass";
    }
  }

  co_await Base::OnPreRender(context);
}

auto MainModule::OnCompositing(engine::FrameContext& context) -> co::Co<>
{
  co_await Base::OnCompositing(context);
}

auto MainModule::OnFrameEnd(engine::FrameContext& /*context*/) -> void { }

auto MainModule::ApplyRenderModeFromPanel() -> void
{
  if (!shell_) {
    return;
  }

  auto* fw_pipeline = static_cast<ForwardPipeline*>(pipeline_.get());
  if (!fw_pipeline) {
    return;
  }

  const auto render_mode = shell_->GetRenderingViewMode();
  fw_pipeline->SetRenderMode(render_mode);

  // Apply debug mode. Rendering debug modes take precedence if set.
  auto debug_mode = shell_->GetRenderingDebugMode();
  if (debug_mode == engine::ShaderDebugMode::kDisabled) {
    debug_mode = shell_->GetLightCullingVisualizationMode();
  }
  fw_pipeline->SetShaderDebugMode(debug_mode);
}

} // namespace oxygen::examples::textured_cube
