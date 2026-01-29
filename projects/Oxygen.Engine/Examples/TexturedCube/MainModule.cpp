//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <filesystem>

#include <imgui.h>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Content/AssetLoader.h>
#include <Oxygen/Engine/AsyncEngine.h>
#include <Oxygen/ImGui/ImGuiModule.h>
#include <Oxygen/Platform/Window.h>
#include <Oxygen/Renderer/Passes/ShaderPass.h>
#include <Oxygen/Renderer/Renderer.h>

#include "DemoShell/Runtime/DemoAppContext.h"
#include "TexturedCube/MainModule.h"

namespace oxygen::examples::textured_cube {

MainModule::MainModule(const DemoAppContext& app)
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
  skybox_service_.reset();
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
      skybox_service_ = std::make_unique<SkyboxService>(
        observer_ptr { asset_loader.get() }, observer_ptr { scene_.get() });
    }

    scene_setup_ = std::make_unique<SceneSetup>(observer_ptr { scene_.get() });
  }
  context.SetScene(observer_ptr { scene_.get() });
}

auto MainModule::OnSceneMutation(engine::FrameContext& context) -> co::Co<>
{
  DCHECK_NOTNULL_F(app_window_);
  DCHECK_NOTNULL_F(scene_);

  UpdateFrameContext(context, [this](int w, int h) {
    if (camera_controller_) {
      camera_controller_->EnsureCamera(observer_ptr { scene_.get() }, w, h);
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

  // Sync texture selection state from UI.
  if (debug_ui_) {
    sphere_texture_.mode = debug_ui_->GetSphereTextureState().mode;
    sphere_texture_.resource_index
      = debug_ui_->GetSphereTextureState().resource_index;
    cube_texture_.mode = debug_ui_->GetCubeTextureState().mode;
    cube_texture_.resource_index
      = debug_ui_->GetCubeTextureState().resource_index;
  }

  // Rebuild cube if needed
  if (cube_needs_rebuild_ && scene_setup_) {
    auto asset_loader = app_.engine ? app_.engine->GetAssetLoader() : nullptr;
    if ((sphere_texture_.mode == SceneSetup::TextureIndexMode::kForcedError
          || cube_texture_.mode == SceneSetup::TextureIndexMode::kForcedError)
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

    auto material = scene_setup_->RebuildCube(sphere_texture_.mode,
      sphere_texture_.resource_index, sphere_texture_.resource_key,
      cube_texture_.mode, cube_texture_.resource_index,
      cube_texture_.resource_key, forced_error_key_, uv_scale, uv_offset,
      metalness, roughness, base_color_rgba, disable_texture_sampling);

    if (material) {
      cube_needs_rebuild_ = false;

      if (auto* renderer = ResolveRenderer(); renderer) {
        if (auto sphere_material = scene_setup_->GetSphereMaterial();
          sphere_material) {
          // TODO: Apply baseline UV transform from MaterialAsset defaults and
          // move this override to per-instance material overrides when the
          // MaterialInstance system is available.
          (void)renderer->OverrideMaterialUvTransform(
            *sphere_material, uv_scale, uv_offset);
        }
        if (auto cube_material = scene_setup_->GetCubeMaterial();
          cube_material) {
          // TODO: Apply baseline UV transform from MaterialAsset defaults and
          // move this override to per-instance material overrides when the
          // MaterialInstance system is available.
          (void)renderer->OverrideMaterialUvTransform(
            *cube_material, uv_scale, uv_offset);
        }
      }
    }
  }

  // Keep UV transform override sticky
  if (scene_setup_) {
    if (auto* renderer = ResolveRenderer(); renderer && debug_ui_) {
      const auto [uv_scale, uv_offset] = debug_ui_->GetEffectiveUvTransform();
      if (auto sphere_material = scene_setup_->GetSphereMaterial();
        sphere_material) {
        // TODO: Apply baseline UV transform from MaterialAsset defaults and
        // move this override to per-instance material overrides when the
        // MaterialInstance system is available.
        (void)renderer->OverrideMaterialUvTransform(
          *sphere_material, uv_scale, uv_offset);
      }
      if (auto cube_material = scene_setup_->GetCubeMaterial(); cube_material) {
        // TODO: Apply baseline UV transform from MaterialAsset defaults and
        // move this override to per-instance material overrides when the
        // MaterialInstance system is available.
        (void)renderer->OverrideMaterialUvTransform(
          *cube_material, uv_scale, uv_offset);
      }
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

    debug_ui_->Draw(context, *camera_controller_,
      observer_ptr { ResolveRenderer() }, shader_pass_config,
      scene_setup_ ? scene_setup_->GetSphereMaterial() : nullptr,
      scene_setup_ ? scene_setup_->GetCubeMaterial() : nullptr,
      cube_needs_rebuild_);
  }

  if (debug_ui_ && texture_service_) {
    if (debug_ui_->IsImportRequested()) {
      debug_ui_->ClearImportRequest();

      const auto& state = debug_ui_->GetImportState();
      TextureLoadingService::ImportSettings settings {
        .source_path = std::filesystem::path(state.source_path.data()),
        .cooked_root = std::filesystem::path(state.cooked_root.data()),
        .kind
        = static_cast<TextureLoadingService::ImportKind>(state.import_kind),
        .output_format_idx = state.output_format_idx,
        .generate_mips = state.generate_mips,
        .max_mip_levels = state.max_mip_levels,
        .mip_filter_idx = state.mip_filter_idx,
        .flip_y = state.flip_y,
        .force_rgba = state.force_rgba,
        .cube_face_size = state.cube_face_size,
        .layout_idx = state.layout_idx,
      };

      const bool submitted = texture_service_->SubmitImport(settings);
      const auto status = texture_service_->GetImportStatus();
      debug_ui_->SetImportStatus(
        status.message, status.in_flight, status.overall_progress);
      if (!submitted) {
        cube_needs_rebuild_ = true;
      }
    }

    if (debug_ui_->IsRefreshRequested()) {
      debug_ui_->ClearRefreshRequest();

      const auto root_path
        = std::filesystem::path(debug_ui_->GetImportState().cooked_root.data());
      LOG_F(
        INFO, "TexturedCube: refresh requested root='{}'", root_path.string());
      std::string error;
      if (texture_service_->RefreshCookedTextureEntries(root_path, &error)) {
        std::vector<DebugUI::CookedTextureEntry> entries;
        for (const auto& entry : texture_service_->GetCookedTextureEntries()) {
          entries.push_back(DebugUI::CookedTextureEntry {
            .index = entry.index,
            .width = entry.width,
            .height = entry.height,
            .mip_levels = entry.mip_levels,
            .array_layers = entry.array_layers,
            .size_bytes = entry.size_bytes,
            .content_hash = entry.content_hash,
            .format = entry.format,
            .texture_type = entry.texture_type,
          });
        }
        LOG_F(INFO, "TexturedCube: refresh completed entries={} root='{}'",
          entries.size(), root_path.string());
        debug_ui_->SetCookedTextureEntries(std::move(entries));
        debug_ui_->SetImportStatus("Cooked root refreshed", false, 0.0f);
      } else {
        LOG_F(ERROR, "TexturedCube: refresh failed root='{}' error='{}'",
          root_path.string(), error);
        debug_ui_->SetImportStatus(error, false, 0.0f);
      }
    }

    const auto status = texture_service_->GetImportStatus();
    if (!status.message.empty()) {
      debug_ui_->SetImportStatus(
        status.message, status.in_flight, status.overall_progress);
    }

    oxygen::content::import::ImportReport report;
    if (texture_service_->ConsumeImportReport(report)) {
      if (!report.success && !report.diagnostics.empty()) {
        debug_ui_->SetImportStatus(
          report.diagnostics.front().message, false, 1.0f);
      }

      std::string error;
      if (texture_service_->RefreshCookedTextureEntries(
            report.cooked_root, &error)) {
        std::vector<DebugUI::CookedTextureEntry> entries;
        for (const auto& entry : texture_service_->GetCookedTextureEntries()) {
          entries.push_back(DebugUI::CookedTextureEntry {
            .index = entry.index,
            .width = entry.width,
            .height = entry.height,
            .mip_levels = entry.mip_levels,
            .array_layers = entry.array_layers,
            .size_bytes = entry.size_bytes,
            .content_hash = entry.content_hash,
            .format = entry.format,
            .texture_type = entry.texture_type,
          });
        }
        debug_ui_->SetCookedTextureEntries(std::move(entries));
        debug_ui_->SetImportStatus("Cooked root refreshed", false, 1.0f);
      } else {
        debug_ui_->SetImportStatus(error, false, 1.0f);
      }
    }

    DebugUI::BrowserAction action {};
    if (debug_ui_->ConsumeBrowserAction(action)) {
      debug_ui_->SetImportStatus("Loading cooked texture...", true, 0.0f);

      texture_service_->StartLoadCookedTexture(action.entry_index,
        [this, action](TextureLoadingService::LoadResult result) {
          if (!result.success) {
            debug_ui_->SetImportStatus(result.status_message, false, 0.0f);
            return;
          }

          debug_ui_->SetImportStatus(result.status_message, false, 1.0f);

          switch (action.type) {
          case DebugUI::BrowserAction::Type::kSetSphere:
            sphere_texture_.mode = SceneSetup::TextureIndexMode::kCustom;
            sphere_texture_.resource_index = action.entry_index;
            sphere_texture_.resource_key = result.resource_key;
            debug_ui_->GetSphereTextureState().mode
              = SceneSetup::TextureIndexMode::kCustom;
            debug_ui_->GetSphereTextureState().resource_index
              = action.entry_index;
            cube_needs_rebuild_ = true;
            break;
          case DebugUI::BrowserAction::Type::kSetCube:
            cube_texture_.mode = SceneSetup::TextureIndexMode::kCustom;
            cube_texture_.resource_index = action.entry_index;
            cube_texture_.resource_key = result.resource_key;
            debug_ui_->GetCubeTextureState().mode
              = SceneSetup::TextureIndexMode::kCustom;
            debug_ui_->GetCubeTextureState().resource_index
              = action.entry_index;
            cube_needs_rebuild_ = true;
            break;
          case DebugUI::BrowserAction::Type::kSetSkybox:
            if (skybox_service_
              && result.texture_type == oxygen::TextureType::kTextureCube) {
              skybox_service_->SetSkyboxResourceKey(result.resource_key);
              auto& lighting = debug_ui_->GetLightingState();
              skybox_service_->ApplyToScene(SkyboxService::SkyLightParams {
                .intensity = lighting.sky_light_intensity,
                .diffuse_intensity = lighting.sky_light_diffuse,
                .specular_intensity = lighting.sky_light_specular,
              });
            }
            break;
          case DebugUI::BrowserAction::Type::kNone:
          default:
            break;
          }
        });
    }
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
