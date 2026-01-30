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
#include <Oxygen/Content/AssetLoader.h>
#include <Oxygen/Engine/AsyncEngine.h>
#include <Oxygen/ImGui/ImGuiModule.h>
#include <Oxygen/Platform/Window.h>
#include <Oxygen/Renderer/Passes/ShaderPass.h>
#include <Oxygen/Renderer/Renderer.h>

#include "DemoShell/Runtime/DemoAppContext.h"
#include "DemoShell/Services/FileBrowserService.h"
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

auto MainModule::OnAttached(observer_ptr<AsyncEngine> engine) noexcept -> bool
{
  if (!engine) {
    return false;
  }

  if (!Base::OnAttached(engine)) {
    return false;
  }

  // initialize Demo Shell
  shell_ = std::make_unique<DemoShell>();

  DemoShellConfig shell_config;

  file_browser_service_ = std::make_unique<FileBrowserService>();
  file_browser_service_->ConfigureContentRoots(ContentRootConfig {
    .content_root = content_root_,
    .cooked_root = cooked_root_,
  });
  shell_config.file_browser_service
    = observer_ptr { file_browser_service_.get() };

  shell_config.input_system = observer_ptr { app_.input_system.get() };

  shell_config.panel_config.content_loader
    = false; // We use custom texture loader
  shell_config.panel_config.camera_controls = true;
  shell_config.panel_config.lighting = true;
  shell_config.panel_config.environment = true;
  shell_config.panel_config.rendering = true;

  // Shared services wiring
  // Note: We initialize SkyboxService later when Scene is ready,
  // so we update the shell with it later.

  shell_config.get_renderer
    = [this]() { return observer_ptr { ResolveRenderer() }; };

  shell_config.get_pass_config_refs = [this]() {
    oxygen::examples::ui::PassConfigRefs refs;
    if (auto render_graph = GetRenderGraph()) {
      refs.shader_pass_config
        = observer_ptr { render_graph->GetShaderPassConfig().get() };
      refs.light_culling_pass_config
        = observer_ptr { render_graph->GetLightCullingPassConfig().get() };
    }
    return refs;
  };

  if (!shell_->Initialize(shell_config)) {
    LOG_F(ERROR, "TexturedCube: DemoShell initialization failed");
    return false;
  }

  // Custom UI components
  // TextureService is initialized in OnExampleFrameStart because it needs
  // asset_loader (which is available, but consistent with old code). Actually,
  // engine is available here, so we can init services here if we want? Old code
  // did it in OnExampleFrameStart. Let's stick to that for Scene dependency.

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
  scene_ = nullptr;
}

auto MainModule::OnFrameStart(engine::FrameContext& context) -> void
{
  Base::OnFrameStart(context);
}

auto MainModule::OnExampleFrameStart(engine::FrameContext& context) -> void
{
  if (!scene_) {
    // 1. Create Scene and transfer to Shell
    auto scene_unique = std::make_unique<scene::Scene>("TexturedCube-Scene");
    auto* scene_ptr = scene_unique.get();
    shell_->SetScene(std::move(scene_unique));
    scene_ = observer_ptr<scene::Scene>(scene_ptr);

    // 2. Initialize Services
    auto asset_loader = app_.engine ? app_.engine->GetAssetLoader() : nullptr;
    if (asset_loader) {
      texture_service_ = std::make_unique<TextureLoadingService>(
        observer_ptr { asset_loader.get() });
      skybox_service_ = std::make_unique<SkyboxService>(
        observer_ptr { asset_loader.get() }, scene_);

      // Init VM
      texture_vm_ = std::make_unique<ui::TextureBrowserVm>(
        observer_ptr { texture_service_.get() },
        observer_ptr { file_browser_service_.get() });
      texture_vm_->SetCubeRebuildNeeded();

      texture_vm_->SetOnSkyboxSelected(
        [this](oxygen::content::ResourceKey key) {
          if (skybox_service_) {
            skybox_service_->SetSkyboxResourceKey(key);
            SkyboxService::SkyLightParams params;
            params.intensity = 1.0f;
            params.diffuse_intensity = 1.0f;
            params.specular_intensity = 1.0f;
            skybox_service_->ApplyToScene(params);
          }
        });

      // Init Panel
      texture_panel_ = std::make_shared<ui::TextureBrowserPanel>();
      texture_panel_->Initialize(observer_ptr { texture_vm_.get() });

      shell_->RegisterPanel(texture_panel_);
      shell_->SetActivePanel("Texture Browser"); // Optional: set active
      shell_->SetSkyboxService(observer_ptr { skybox_service_.get() });
    }

    // 3. Initialize Scene Setup
    // Ensure services are ready before passing to SceneSetup
    if (texture_service_ && skybox_service_) {
      scene_setup_ = std::make_unique<SceneSetup>(
        scene_, *texture_service_, *skybox_service_, cooked_root_);

      // Initial Scene Content
      scene_setup_->EnsureCubeNode();
      scene_setup_->EnsureEnvironment({});
      scene_setup_->EnsureLighting({}, {});
    }

    // 4. Setup Camera
    auto camera_node = scene_ptr->CreateNode("MainCamera");
    auto transform = camera_node.GetTransform();
    const glm::vec3 position { 0.0f, 2.0f, -5.0f };
    const glm::vec3 target { 0.0f, 0.0f, 0.0f };
    const glm::vec3 up { 0.0f, 1.0f, 0.0f };

    transform.SetLocalPosition(position);
    const auto view_mat = glm::lookAt(position, target, up);
    const auto world_rot = glm::quat_cast(glm::inverse(view_mat));
    transform.SetLocalRotation(world_rot);

    // Attach Perspective Camera Component
    auto camera = std::make_unique<oxygen::scene::PerspectiveCamera>();
    camera->SetFieldOfView(60.0f);
    camera->SetNearPlane(0.1f);
    camera->SetFarPlane(1000.0f);
    camera_node.AttachCamera(std::move(camera));

    // Defer SetActiveCamera to OnPreRender to ensure valid world transforms
    pending_camera_node_ = std::move(camera_node);
  }

  if (auto scene_ptr = shell_->TryGetScene()) {
    context.SetScene(scene_ptr);
  }
}

auto MainModule::OnSceneMutation(engine::FrameContext& context) -> co::Co<>
{
  DCHECK_NOTNULL_F(app_window_);

  auto scene_ptr = shell_ ? shell_->TryGetScene() : nullptr;
  if (!scene_ptr) {
    co_return; // Not ready
  }

  UpdateFrameContext(context, [this](int w, int h) {
    last_viewport_w_ = w;
    last_viewport_h_ = h;
    if (shell_) {
      shell_->GetCameraLifecycle().EnsureViewport(w, h);
    }
  });

  if (!app_window_->GetWindow()) {
    co_return;
  }

  // Shell Update
  if (shell_) {
    shell_->Update(time::CanonicalDuration {});
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
        && forced_error_key_ == static_cast<oxygen::content::ResourceKey>(0)
        && asset_loader) {
        forced_error_key_ = asset_loader->MintSyntheticTextureKey();
      }

      auto& surface = texture_vm_->GetSurfaceState();

      auto [uv_scale, uv_offset] = texture_vm_->GetEffectiveUvTransform();

      glm::vec4 base_color { 1.0f };
      if (surface.use_constant_base_color) {
        base_color = { surface.constant_base_color_rgb, 1.0f };
      }

      // Rebuild Cube
      auto material = scene_setup_->RebuildCube(sphere_state.mode,
        sphere_state.resource_index, sphere_state.resource_key, cube_state.mode,
        cube_state.resource_index, cube_state.resource_key, forced_error_key_,
        uv_scale, uv_offset, surface.metalness, surface.roughness, base_color,
        surface.use_constant_base_color);

      // Apply overrides immediately
      if (auto* renderer = ResolveRenderer(); renderer && material) {
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
      if (auto* renderer = ResolveRenderer()) {
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

  co_return;
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

  if (file_browser_service_) {
    file_browser_service_->UpdateAndDraw();
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

  if (shell_) {
    ApplyRenderModeFromPanel();
  }

  if (auto rg = GetRenderGraph(); rg) {
    rg->SetupRenderPasses();

    if (auto shader_pass_config = rg->GetShaderPassConfig();
      shader_pass_config) {
      shader_pass_config->clear_color
        = graphics::Color { 0.08F, 0.08F, 0.10F, 1.0F };
      shader_pass_config->debug_name = "ShaderPass";
    }
  }

  EnsureViewCameraRegistered();

  if (shell_ && pending_camera_node_.IsAlive()) {
    shell_->SetActiveCamera(std::move(pending_camera_node_));
    shell_->GetCameraLifecycle().CaptureInitialPose();
    // pending_camera_node_ is now invalid (moved), which is correct
  }

  co_return;
}

auto MainModule::OnCompositing(engine::FrameContext& context) -> co::Co<>
{
  MarkSurfacePresentable(context);
  co_return;
}

auto MainModule::OnFrameEnd(engine::FrameContext& /*context*/) -> void { }

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
    LOG_F(INFO, "TexturedCube: Active camera changed; re-registering view");
  }

  RegisterViewForRendering(active_camera);
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

    using namespace oxygen::examples::ui;
    using oxygen::graphics::FillMode;
    const auto view_mode = shell_->GetRenderingViewMode();
    const FillMode mode = (view_mode == RenderingViewMode::kWireframe)
      ? FillMode::kWireFrame
      : FillMode::kSolid;
    render_graph->SetWireframeEnabled(mode == FillMode::kWireFrame);
    shader_pass_config->fill_mode = mode;
    transparent_pass_config->fill_mode = mode;

    const bool force_clear = (mode == FillMode::kWireFrame);
    shader_pass_config->clear_color_target = true;
    shader_pass_config->auto_skip_clear_when_sky_pass_present = !force_clear;

    // Apply debug mode. Rendering debug modes take precedence if set.
    auto debug_mode = shell_->GetRenderingDebugMode();
    if (debug_mode == engine::ShaderDebugMode::kDisabled) {
      debug_mode = shell_->GetLightCullingVisualizationMode();
    }
    shader_pass_config->debug_mode = debug_mode;
  }
}

} // namespace oxygen::examples::textured_cube
