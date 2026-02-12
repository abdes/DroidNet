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

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Base/Macros.h>
#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Content/IAssetLoader.h>
#include <Oxygen/Engine/AsyncEngine.h>
#include <Oxygen/ImGui/ImGuiModule.h>
#include <Oxygen/Renderer/Renderer.h>

#include "DemoShell/Runtime/CompositionView.h"
#include "DemoShell/Runtime/DemoAppContext.h"
#include "DemoShell/Runtime/ForwardPipeline.h"
#include "DemoShell/Services/FileBrowserService.h"
#include "DemoShell/Services/SettingsService.h"
#include "DemoShell/Services/SkyboxService.h"
#include "DemoShell/UI/DemoShellUi.h"
#include "TexturedCube/MainModule.h"
#include <Oxygen/Scene/Camera/Perspective.h>

namespace oxygen::examples::textured_cube {

MainModule::MainModule(const DemoAppContext& app)
  : Base(app)
  , last_viewport_({ 0, 0 })
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
  constexpr auto kWidth = 2560U;
  constexpr auto kHeight = 1400U;

  platform::window::Properties p("Textured Cube (Demo Shell)");
  p.extent = { .width = kWidth, .height = kHeight };
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

auto MainModule::OnAttachedImpl(observer_ptr<AsyncEngine> engine) noexcept
  -> std::unique_ptr<DemoShell>
{
  if (!engine) {
    return nullptr;
  }

  // Create Pipeline
  auto fw_pipeline
    = std::make_unique<ForwardPipeline>(observer_ptr { app_.engine.get() });

  pipeline_ = std::move(fw_pipeline);

  auto shell = std::make_unique<DemoShell>();
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
  shell_config.panel_config.post_process = true;
  shell_config.enable_camera_rig = true;

  shell_config.get_active_pipeline
    = [this]() -> observer_ptr<RenderingPipeline> {
    return observer_ptr { pipeline_.get() };
  };

  if (!shell->Initialize(shell_config)) {
    LOG_F(ERROR, "TexturedCube: DemoShell initialization failed");
    return nullptr;
  }

  // Create Main View ID
  main_view_id_ = GetOrCreateViewId("MainView");

  LOG_F(INFO, "TexturedCube: Module initialized");
  return shell;
}

auto MainModule::OnShutdown() noexcept -> void
{
  // Clear scene from shell first to ensure controlled destruction
  auto& shell = GetShell();
  shell.SetScene(nullptr);

  // Destroy setup before other services
  scene_setup_.reset();

  skybox_service_.reset();
  texture_service_.reset();

  texture_panel_.reset();
  texture_vm_.reset();

  last_viewport_ = { 0, 0 };

  // Clear observers
  active_scene_ = {};
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

  if (!active_scene_.IsValid()) {
    // 1. Create Scene and transfer to Shell
    auto scene_unique = std::make_unique<scene::Scene>("TexturedCube-Scene");
    active_scene_ = shell.SetScene(std::move(scene_unique));

    if (!main_camera_.IsAlive()) {
      if (const auto scene_ptr = shell.TryGetScene()) {
        main_camera_ = scene_ptr->CreateNode("MainCamera");
        auto camera = std::make_unique<scene::PerspectiveCamera>();
        const bool attached = main_camera_.AttachCamera(std::move(camera));
        CHECK_F(attached, "Failed to attach PerspectiveCamera to MainCamera");
      }
    }

    // 2. Initialize Services
    auto asset_loader = app_.engine ? app_.engine->GetAssetLoader() : nullptr;
    if (asset_loader) {
      texture_service_ = std::make_unique<TextureLoadingService>(
        observer_ptr<oxygen::content::IAssetLoader> { asset_loader.get() });
      skybox_service_ = std::make_unique<SkyboxService>(
        observer_ptr<oxygen::content::IAssetLoader> { asset_loader.get() },
        observer_ptr { active_scene_.operator->() });

      // Init VM
      texture_vm_ = std::make_unique<ui::MaterialsSandboxVm>(
        observer_ptr { texture_service_.get() },
        observer_ptr { &shell.GetFileBrowserService() });
      texture_vm_->SetCubeRebuildNeeded();

      texture_vm_->SetOnSkyboxSelected(
        [this](oxygen::content::ResourceKey key) {
          if (skybox_service_) {
            skybox_service_->SetSkyboxResourceKey(key);
            SkyboxService::SkyLightParams params;
            if (const auto settings = SettingsService::ForDemoApp()) {
              if (const auto intensity
                = settings->GetFloat("env.sky_sphere.intensity")) {
                params.sky_sphere_intensity = *intensity;
              }
            }
            params.intensity_mul = 1.0F;
            params.diffuse_intensity = 1.0F;
            params.specular_intensity = 1.0F;
            skybox_service_->ApplyToScene(params);
          }
        });

      // Init Panel
      texture_panel_ = std::make_shared<ui::MaterialsSandboxPanel>();
      texture_panel_->Initialize(observer_ptr { texture_vm_.get() });

      shell.RegisterPanel(texture_panel_);

      // 3. Initialize Scene Setup
      scene_setup_ = std::make_unique<SceneSetup>(
        observer_ptr { active_scene_.operator->() }, *texture_service_,
        *skybox_service_, cooked_root_);
      scene_setup_->Initialize();
    }
  }

  if (!app_.headless && app_window_ && app_window_->GetWindow()) {
    last_viewport_ = app_window_->GetWindow()->Size();
  }

  frame_context.SetScene(shell.TryGetScene());
}

auto MainModule::OnSceneMutation(observer_ptr<engine::FrameContext> context)
  -> co::Co<>
{
  DCHECK_NOTNULL_F(app_window_);
  if (!app_window_->GetWindow()) {
    co_return;
  }

  if (!active_scene_.IsValid()) {
    co_return;
  }

  // Ensure scene objects (idempotent)
  if (scene_setup_) {
    scene_setup_->Initialize();
  }

  // Update Texture State
  if (texture_vm_ && scene_setup_) {
    // Check for Rebuild
    if (texture_vm_->IsCubeRebuildNeeded()) {
      texture_vm_->ClearCubeRebuildNeeded();

      auto& sphere_slot = texture_vm_->GetSphereTextureState();
      auto& cube_slot = texture_vm_->GetCubeTextureState();
      auto& surface_vm = texture_vm_->GetSurfaceState();

      auto asset_loader = app_.engine ? app_.engine->GetAssetLoader() : nullptr;
      if ((sphere_slot.mode == TextureIndexMode::kForcedError
            || cube_slot.mode == TextureIndexMode::kForcedError)
        && (forced_error_key_ == oxygen::content::ResourceKey { 0U })
        && asset_loader) {
        forced_error_key_ = asset_loader->MintSyntheticTextureKey();
      }

      const std::pair<glm::vec2, glm::vec2> uv_transform
        = texture_vm_->GetEffectiveUvTransform();

      glm::vec4 surface_base_color { 1.0F };
      if (surface_vm.use_constant_base_color) {
        surface_base_color = { surface_vm.constant_base_color_rgb, 1.0F };
      }

      // Rebuild Cube
      ObjectTextureState sphere_state;
      sphere_state.mode = sphere_slot.mode;
      sphere_state.resource_index = sphere_slot.resource_index;
      sphere_state.resource_key = sphere_slot.resource_key;

      ObjectTextureState cube_state;
      cube_state.mode = cube_slot.mode;
      cube_state.resource_index = cube_slot.resource_index;
      cube_state.resource_key = cube_slot.resource_key;

      SurfaceParams surface_params;
      surface_params.metalness = surface_vm.metalness;
      surface_params.roughness = surface_vm.roughness;
      surface_params.base_color = surface_base_color;
      surface_params.disable_texture_sampling
        = surface_vm.use_constant_base_color;

      // Update each object independently so callers can later update only one
      scene_setup_->UpdateSphere(
        sphere_state, surface_params, forced_error_key_);
      scene_setup_->UpdateCube(cube_state, surface_params, forced_error_key_);

      // Apply overrides immediately
      if (app_.engine) {
        if (auto r = app_.engine->GetModule<engine::Renderer>()) {
          scene_setup_->UpdateUvTransform(
            r->get(), uv_transform.first, uv_transform.second);
        }
      }
    } else {
      // Sticky Overrides even if not rebuilt (e.g. UV changed)
      if (app_.engine) {
        if (auto r = app_.engine->GetModule<engine::Renderer>()) {
          const std::pair<glm::vec2, glm::vec2> uv_transform
            = texture_vm_->GetEffectiveUvTransform();
          scene_setup_->UpdateUvTransform(
            r->get(), uv_transform.first, uv_transform.second);
        }
      }
    }
  }

  // Delegate to pipeline
  co_await Base::OnSceneMutation(context);
}

auto MainModule::OnGameplay(observer_ptr<engine::FrameContext> context)
  -> co::Co<>
{
  auto& shell = GetShell();
  shell.Update(context->GetGameDeltaTime());
  co_return;
}

auto MainModule::OnGuiUpdate(observer_ptr<engine::FrameContext> context)
  -> co::Co<>
{
  // We only draw ImGui if we still have an app window open
  DCHECK_NOTNULL_F(app_window_);
  if (!app_window_->GetWindow()) {
    co_return;
  }

  auto& shell = GetShell();
  shell.Draw(context);

  co_return;
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
  main_comp.with_atmosphere = true;
  shell.OnMainViewReady(context, main_comp);
  views.push_back(std::move(main_comp));

  // Also render our tools layer
  const auto imgui_view_id = GetOrCreateViewId("ImGuiView");
  views.push_back(CompositionView::ForImGui(
    imgui_view_id, view, [](graphics::CommandRecorder&) { }));
}

} // namespace oxygen::examples::textured_cube
