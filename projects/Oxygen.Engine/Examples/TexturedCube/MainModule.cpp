//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include "MainModule.h"

#include <imgui.h>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Content/AssetLoader.h>
#include <Oxygen/Engine/AsyncEngine.h>
#include <Oxygen/Graphics/Common/Graphics.h>
#include <Oxygen/ImGui/ImGuiModule.h>
#include <Oxygen/Renderer/Passes/ShaderPass.h>
#include <Oxygen/Renderer/Renderer.h>
#include <Oxygen/Scene/Camera/Perspective.h>

namespace oxygen::examples::textured_cube {

MainModule::MainModule(const common::AsyncEngineApp& app)
  : Base(app)
{
  DCHECK_NOTNULL_F(app_.platform);
  DCHECK_F(!app_.gfx_weak.expired());
}

auto MainModule::BuildDefaultWindowProperties() const
  -> platform::window::Properties
{
  platform::window::Properties p("Oxygen Example");
  p.extent = { .width = 2560U, .height = 960U };
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

  // Initialize camera controller
  camera_controller_ = std::make_unique<CameraController>(app_.input_system);
  if (!camera_controller_->InitInputBindings()) {
    LOG_F(ERROR, "Failed to initialize camera input bindings");
    return false;
  }

  // Initialize debug UI
  debug_ui_ = std::make_unique<DebugUI>();

  return true;
}

auto MainModule::OnShutdown() noexcept -> void
{
  camera_controller_.reset();
  texture_service_.reset();
  skybox_manager_.reset();
  scene_setup_.reset();
  debug_ui_.reset();
}

auto MainModule::OnFrameStart(engine::FrameContext& context) -> void
{
  Base::OnFrameStart(context);
}

auto MainModule::OnExampleFrameStart(engine::FrameContext& context) -> void
{
  if (!scene_) {
    scene_ = std::make_shared<scene::Scene>("TexturedCube-Scene");

    // Initialize services that depend on scene
    auto asset_loader = app_.engine ? app_.engine->GetAssetLoader() : nullptr;
    if (asset_loader) {
      texture_service_ = std::make_unique<TextureLoadingService>(
        observer_ptr { asset_loader.get() });
      skybox_manager_ = std::make_unique<SkyboxManager>(
        observer_ptr { asset_loader.get() }, scene_);
    }

    scene_setup_ = std::make_unique<SceneSetup>(scene_);
  }
  context.SetScene(observer_ptr { scene_.get() });
}

auto MainModule::OnSceneMutation(engine::FrameContext& context) -> co::Co<>
{
  DCHECK_NOTNULL_F(app_window_);
  DCHECK_NOTNULL_F(scene_);

  UpdateFrameContext(context, [this](int w, int h) {
    if (camera_controller_) {
      camera_controller_->EnsureCamera(scene_, w, h);
      RegisterViewForRendering(camera_controller_->GetCameraNode());
    }
  });

  if (!app_window_->GetWindow()) {
    co_return;
  }

  // Ensure scene objects
  if (scene_setup_) {
    scene_setup_->EnsureCubeNode();
    scene_setup_->EnsureEnvironment({});
    scene_setup_->EnsureLighting({}, {});
  }

  // Handle texture loading requests from debug UI
  if (debug_ui_ && debug_ui_->IsTextureLoadRequested() && texture_service_) {
    debug_ui_->ClearTextureLoadRequest();

    auto& tex_state = debug_ui_->GetTextureState();
    TextureLoadingService::LoadOptions options {
      .output_format_idx = tex_state.output_format_idx,
      .generate_mips = tex_state.generate_mips,
      .tonemap_hdr_to_ldr = tex_state.tonemap_hdr_to_ldr,
      .hdr_exposure_ev = tex_state.hdr_exposure_ev,
    };

    auto result = co_await texture_service_->LoadTextureAsync(
      std::string { tex_state.path.data() }, options);

    tex_state.status_message = result.status_message;
    tex_state.last_width = result.width;
    tex_state.last_height = result.height;

    if (result.success) {
      custom_texture_key_ = result.resource_key;
      if (custom_texture_resource_index_ == 0U) {
        custom_texture_resource_index_ = 1U;
      } else {
        ++custom_texture_resource_index_;
      }
      texture_index_mode_ = SceneSetup::TextureIndexMode::kCustom;
      cube_needs_rebuild_ = true;
    }
  }

  // Handle skybox loading requests from debug UI
  if (debug_ui_ && debug_ui_->IsSkyboxLoadRequested() && skybox_manager_) {
    debug_ui_->ClearSkyboxLoadRequest();

    auto& sky_state = debug_ui_->GetSkyboxState();
    SkyboxManager::LoadOptions options {
      .layout = static_cast<SkyboxManager::Layout>(sky_state.layout_idx),
      .output_format
      = static_cast<SkyboxManager::OutputFormat>(sky_state.output_format_idx),
      .cube_face_size = sky_state.cube_face_size,
      .flip_y = sky_state.flip_y,
    };

    auto result = co_await skybox_manager_->LoadSkyboxAsync(
      std::string { sky_state.path.data() }, options);

    sky_state.status_message = result.status_message;
    sky_state.last_face_size = result.face_size;

    if (result.success && debug_ui_) {
      auto& lighting = debug_ui_->GetLightingState();
      skybox_manager_->ApplyToScene(SkyboxManager::SkyLightParams {
        .intensity = lighting.sky_light_intensity,
        .diffuse_intensity = lighting.sky_light_diffuse,
        .specular_intensity = lighting.sky_light_specular,
      });
    }
  }

  // Rebuild cube if needed
  if (cube_needs_rebuild_ && scene_setup_) {
    auto asset_loader = app_.engine ? app_.engine->GetAssetLoader() : nullptr;
    if (texture_index_mode_ == SceneSetup::TextureIndexMode::kForcedError
      && forced_error_key_ == static_cast<oxygen::content::ResourceKey>(0)
      && asset_loader) {
      forced_error_key_ = asset_loader->MintSyntheticTextureKey();
    }

    glm::vec2 uv_scale { 1.0f, 1.0f };
    glm::vec2 uv_offset { 0.0f, 0.0f };
    float metalness = 0.85f;
    float roughness = 0.12f;
    glm::vec4 base_color_rgba { 1.0f, 1.0f, 1.0f, 1.0f };
    bool disable_texture_sampling = false;
    if (debug_ui_) {
      auto [scale, offset] = debug_ui_->GetEffectiveUvTransform();
      uv_scale = scale;
      uv_offset = offset;

      auto& surface = debug_ui_->GetSurfaceState();
      metalness = surface.metalness;
      roughness = surface.roughness;

      if (surface.use_constant_base_color) {
        base_color_rgba = { surface.constant_base_color_rgb, 1.0f };
        // The fallback texture is a valid texture and would be sampled if we
        // didn't explicitly disable sampling here. When the user requests a
        // constant base color, we must set the flag to ensure the solid color
        // is used without modulation by the fallback texture.
        disable_texture_sampling = true;
      }
    }

    auto material = scene_setup_->RebuildCube(texture_index_mode_,
      custom_texture_resource_index_, custom_texture_key_, forced_error_key_,
      uv_scale, uv_offset, metalness, roughness, base_color_rgba,
      disable_texture_sampling);

    if (material) {
      cube_needs_rebuild_ = false;

      if (auto* renderer = ResolveRenderer(); renderer) {
        (void)renderer->OverrideMaterialUvTransform(
          *material, uv_scale, uv_offset);
      }
    }
  }

  // Keep UV transform override sticky
  if (auto material = scene_setup_ ? scene_setup_->GetCubeMaterial() : nullptr;
    material) {
    if (auto* renderer = ResolveRenderer(); renderer && debug_ui_) {
      const auto [uv_scale, uv_offset] = debug_ui_->GetEffectiveUvTransform();
      (void)renderer->OverrideMaterialUvTransform(
        *material, uv_scale, uv_offset);
    }
  }

  // Update sun light from debug UI
  if (scene_setup_ && debug_ui_) {
    auto& lighting = debug_ui_->GetLightingState();
    scene_setup_->UpdateSunLight(SceneSetup::SunLightParams {
      .intensity = lighting.sun_intensity,
      .color_rgb = lighting.sun_color_rgb,
    });
  }

  // Update camera
  if (camera_controller_) {
    camera_controller_->Update();
  }

  co_return;
}

auto MainModule::OnGameplay(engine::FrameContext& /*context*/) -> co::Co<>
{
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

  if (debug_ui_ && camera_controller_) {
    oxygen::engine::ShaderPassConfig* shader_pass_config = nullptr;
    if (auto rg = GetRenderGraph(); rg) {
      if (auto cfg = rg->GetShaderPassConfig(); cfg) {
        shader_pass_config = cfg.get();
      }
    }

    debug_ui_->Draw(context, *camera_controller_, texture_index_mode_,
      custom_texture_resource_index_, observer_ptr { ResolveRenderer() },
      shader_pass_config,
      scene_setup_ ? scene_setup_->GetCubeMaterial() : nullptr,
      cube_needs_rebuild_);
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

  if (auto rg = GetRenderGraph(); rg) {
    rg->SetupRenderPasses();

    if (auto shader_pass_config = rg->GetShaderPassConfig();
      shader_pass_config) {
      shader_pass_config->clear_color
        = graphics::Color { 0.08F, 0.08F, 0.10F, 1.0F };
      shader_pass_config->debug_name = "ShaderPass";
    }
  }

  co_return;
}

auto MainModule::OnCompositing(engine::FrameContext& context) -> co::Co<>
{
  MarkSurfacePresentable(context);
  co_return;
}

auto MainModule::OnFrameEnd(engine::FrameContext& /*context*/) -> void { }

} // namespace oxygen::examples::textured_cube
