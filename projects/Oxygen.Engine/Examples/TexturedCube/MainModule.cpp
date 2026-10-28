//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <cmath>
#include <cstddef>
#include <filesystem>
#include <memory>
#include <source_location>
#include <utility>
#include <vector>

#include "DemoShell/DemoShell.h"
#include "DemoShell/Services/FileBrowserService.h"
#include "DemoShell/Services/SettingsService.h"
#include "DemoShell/Services/SkyboxService.h"
#include "DemoShell/UI/DemoShellUi.h"
#include "TexturedCube/MainModule.h"
#include "TexturedCube/SceneSetup.h"
#include "TexturedCube/TextureLoadingService.h"
#include "TexturedCube/UI/MaterialsSandboxPanel.h"
#include <glm/ext/quaternion_float.hpp>
#include <glm/ext/vector_float4.hpp>
#include <glm/trigonometric.hpp>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Content/IAssetLoader.h>
#include <Oxygen/Content/ResourceKey.h>
#include <Oxygen/Core/Transforms/NodeFrame.h>
#include <Oxygen/Core/Types/AssetKey.h>
#include <Oxygen/Core/Types/View.h>
#include <Oxygen/Data/MaterialAsset.h>
#include <Oxygen/Engine/AsyncEngine.h>
#include <Oxygen/Engine/IAsyncEngine.h>
#include <Oxygen/Host/HostConfig.h>
#include <Oxygen/Host/HostModule.h>
#include <Oxygen/OxCo/Co.h>
#include <Oxygen/Platform/Window.h>
#include <Oxygen/Scene/Camera/Perspective.h>
#include <Oxygen/Scene/Scene.h>
#include <Oxygen/Vortex/CompositionView.h>

namespace oxygen::examples::textured_cube {

namespace {
  constexpr size_t kDefaultSceneCapacity = 128;
}

MainModule::MainModule(const host::HostContext& host)
  : Base(host)
{
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

auto MainModule::ResolveSelectedCustomMaterial()
  -> std::shared_ptr<const data::MaterialAsset>
{
  if (!texture_vm_ || !texture_vm_->IsUseCustomMaterial()) {
    selected_custom_material_.reset();
    selected_custom_material_key_.reset();
    custom_material_load_inflight_key_.reset();
    return nullptr;
  }

  const auto selected_key = texture_vm_->GetSelectedCustomMaterialKey();
  if (!selected_key.has_value()) {
    selected_custom_material_.reset();
    selected_custom_material_key_.reset();
    custom_material_load_inflight_key_.reset();
    return nullptr;
  }

  if (selected_custom_material_key_.has_value()
    && selected_custom_material_key_.value() == selected_key.value()
    && selected_custom_material_) {
    return selected_custom_material_;
  }

  auto asset_loader = Host().engine->GetAssetLoader();
  if (!asset_loader) {
    return nullptr;
  }

  if (auto cached = asset_loader->GetMaterialAsset(selected_key.value())) {
    selected_custom_material_ = cached;
    selected_custom_material_key_ = selected_key.value();
    custom_material_load_inflight_key_.reset();
    return selected_custom_material_;
  }

  RequestCustomMaterialLoad(selected_key.value());
  return nullptr;
}

auto MainModule::RequestCustomMaterialLoad(const data::AssetKey& key) -> void
{
  auto asset_loader = Host().engine->GetAssetLoader();
  if (!asset_loader) {
    return;
  }

  if (custom_material_load_inflight_key_.has_value()
    && custom_material_load_inflight_key_.value() == key) {
    return;
  }

  selected_custom_material_.reset();
  custom_material_load_inflight_key_ = key;
  asset_loader->StartLoadMaterialAsset(key,
    [this, key](const std::shared_ptr<data::MaterialAsset>& material) -> void {
      if (!texture_vm_ || !texture_vm_->IsUseCustomMaterial()) {
        custom_material_load_inflight_key_.reset();
        return;
      }

      const auto selected_key = texture_vm_->GetSelectedCustomMaterialKey();
      if (!selected_key.has_value() || selected_key.value() != key) {
        custom_material_load_inflight_key_.reset();
        return;
      }

      custom_material_load_inflight_key_.reset();
      if (!material) {
        LOG_F(WARNING,
          "TexturedCube: failed to load selected custom material from cooked "
          "root (asset_key={})",
          key);
        return;
      }

      selected_custom_material_ = material;
      selected_custom_material_key_ = key;
      texture_vm_->SetCubeRebuildNeeded();
    });
}

auto MainModule::BuildWindowProperties() const -> platform::window::Properties
{
  constexpr auto kWidth = 2560U;
  constexpr auto kHeight = 1400U;

  auto props = Base::BuildWindowProperties();
  props.title = "Textured Cube (Demo Shell)";
  props.extent = { .width = kWidth, .height = kHeight };
  return props;
}

auto MainModule::OnAttachedImpl(observer_ptr<IAsyncEngine> engine) noexcept
  -> std::unique_ptr<DemoShell>
{
  if (!engine) {
    return nullptr;
  }

  auto shell = std::make_unique<DemoShell>();
  auto shell_config = MakeShellConfig();
  shell_config.content_roots = {
    .content_root = content_root_,
    .cooked_root = cooked_root_,
  };

  // The materials sandbox replaces the content loader.
  shell_config.panel_config.content_loader = false;
  shell_config.panel_config.camera_controls = true;
  shell_config.panel_config.environment = true;
  shell_config.panel_config.diagnostics = true;
  shell_config.panel_config.post_process = true;
  shell_config.panel_config.ground_grid = true;
  shell_config.enable_camera_rig = true;
  shell_config.force_environment_override = false;

  if (!shell->Initialize(shell_config)) {
    LOG_F(ERROR, "TexturedCube: DemoShell initialization failed");
    return nullptr;
  }
  StageScene(*shell);

  main_view_id_ = Publisher().GetOrCreateViewId("MainView");

  LOG_F(INFO, "TexturedCube: Module initialized");
  return shell;
}

auto MainModule::OnShutdown() noexcept -> void
{
#ifdef OXYGEN_BUILD_UI_TESTS
  StopUiTests();
#endif
  main_view_state_.Reset({}, ResolveRenderer(), main_view_id_);
  // Drop the scene before the services that reference it.
  Stage().Clear();

  // Destroy setup before other services
  scene_setup_.reset();

  skybox_service_.reset();
  texture_service_.reset();

  texture_panel_.reset();
  texture_vm_.reset();
  selected_custom_material_.reset();
  selected_custom_material_key_.reset();
  custom_material_load_inflight_key_.reset();
  active_scene_ = {};
  Base::OnShutdown();
}

auto MainModule::OnScenePublished(engine::FrameContext& context) -> void
{
  Base::OnScenePublished(context);
  active_scene_ = Stage().GetActiveScene();
  main_camera_ = Stage().TakePublishedMainCamera();
}

auto MainModule::StageScene(DemoShell& shell) -> void
{
  Stage().Stage(std::make_shared<scene::Scene>(
    "TexturedCube-Scene", kDefaultSceneCapacity));
  const auto staged_scene = Stage().GetStagedScene();

  auto camera_node = staged_scene->CreateNode("MainCamera");
  const bool attached
    = camera_node.AttachCamera(std::make_unique<scene::PerspectiveCamera>());
  CHECK_F(attached, "Failed to attach PerspectiveCamera to MainCamera");
  auto tf = camera_node.GetTransform();
  tf.SetLocalPosition(Vec3 { 0.0F, -6.0F, 3.0F });
  // Face the cube (+Y), pitched 20 degrees down.
  const auto pitch = glm::radians(-20.0F);
  tf.SetLocalRotation(transforms::LookRotation(
    { .forward = Vec3 { 0.0F, std::cos(pitch), std::sin(pitch) } }));
  Stage().SetStagedMainCamera(std::move(camera_node));

  auto asset_loader = Host().engine->GetAssetLoader();
  if (!asset_loader) {
    return;
  }
  const auto loader
    = observer_ptr<oxygen::content::IAssetLoader> { asset_loader.get() };
  skybox_service_ = std::make_unique<SkyboxService>(loader, staged_scene);
  if (!texture_vm_) {
    CreateMaterialsSandbox(shell, loader);
  }
  texture_vm_->SetCubeRebuildNeeded();

  scene_setup_ = std::make_unique<SceneSetup>(
    staged_scene, *texture_service_, *skybox_service_, cooked_root_);
  scene_setup_->Initialize();
}

auto MainModule::CreateMaterialsSandbox(
  DemoShell& shell, observer_ptr<content::IAssetLoader> loader) -> void
{
  texture_service_ = std::make_unique<TextureLoadingService>(loader);
  texture_vm_ = std::make_unique<ui::MaterialsSandboxVm>(
    observer_ptr { texture_service_.get() },
    observer_ptr { &shell.GetFileBrowserService() });
  texture_vm_->SetOnSkyboxSelected(
    [this](oxygen::content::ResourceKey key) -> void {
      if (!skybox_service_) {
        return;
      }
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
    });

  texture_panel_ = std::make_shared<ui::MaterialsSandboxPanel>(
    observer_ptr { texture_vm_.get() }, shell.GetPanelCommands());
  shell.RegisterPanel(texture_panel_);
}

auto MainModule::OnSceneMutation(observer_ptr<engine::FrameContext> context)
  -> co::Co<>
{
  if (!HasRenderableWindow()) {
    co_return;
  }

  if (!active_scene_.IsValid() && !Stage().HasStagedScene()) {
    StageScene(GetShell());
  }

  if (!active_scene_.IsValid()) {
    co_await Base::OnSceneMutation(context);
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

      auto asset_loader = Host().engine->GetAssetLoader();
      if ((sphere_slot.mode == TextureIndexMode::kForcedError
            || cube_slot.mode == TextureIndexMode::kForcedError)
        && (forced_error_key_ == oxygen::content::ResourceKey { 0U })
        && asset_loader) {
        forced_error_key_ = asset_loader->MintSyntheticTextureKey();
      }

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
      const bool grid_selected
        = (sphere_slot.mode == TextureIndexMode::kProceduralGrid
          || cube_slot.mode == TextureIndexMode::kProceduralGrid);
      if (grid_selected) {
        surface_base_color = { 0.72F, 0.70F, 0.68F, 1.0F };
      }
      surface_params.disable_texture_sampling
        = surface_vm.use_constant_base_color || grid_selected;
      const auto custom_material = ResolveSelectedCustomMaterial();

      scene_setup_->UpdateSphere(
        sphere_state, surface_params, forced_error_key_, custom_material);
      scene_setup_->UpdateCube(
        cube_state, surface_params, forced_error_key_, custom_material);
    }
  }

  co_await Base::OnSceneMutation(context);
}

auto MainModule::BuildViews(
  engine::FrameContext& context, host::HostViews& views) -> void
{
  if (!active_scene_.IsValid() || !main_camera_.IsAlive()) {
    main_view_state_.Reset(
      observer_ptr { &context }, ResolveRenderer(), main_view_id_);
    return;
  }
  // An unset viewport covers the window.
  auto main_view
    = vortex::CompositionView::ForScene(main_view_id_, View {}, main_camera_);
  main_view.view_state_handle = main_view_state_
                                  .For(context, ResolveRenderer(),
                                    main_view_id_, active_scene_, main_camera_)
                                  .handle;
  main_view.with_atmosphere = true;
  views.views.push_back(std::move(main_view));
  views.main = main_view_id_;
}

} // namespace oxygen::examples::textured_cube
