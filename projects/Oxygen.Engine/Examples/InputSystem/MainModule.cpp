//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <memory>
#include <source_location>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "DemoShell/DemoShell.h"
#include "DemoShell/PanelCommands.h"
#include "DemoShell/Services/DefaultSceneLighting.h"
#include "InputSystem/InputDebugPanel.h"
#include "InputSystem/MainModule.h"
#include <glm/ext/quaternion_float.hpp>
#include <glm/ext/vector_float4.hpp>
#include <glm/trigonometric.hpp>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Core/Constants.h>
#include <Oxygen/Core/FrameContext.h>
#include <Oxygen/Core/Transforms/NodeFrame.h>
#include <Oxygen/Core/Types/AssetKey.h>
#include <Oxygen/Core/Types/View.h>
#include <Oxygen/Data/AssetType.h>
#include <Oxygen/Data/GeometryAsset.h>
#include <Oxygen/Data/MaterialAsset.h>
#include <Oxygen/Data/MaterialDomain.h>
#include <Oxygen/Data/PakFormat.h>
#include <Oxygen/Data/ProceduralMeshes.h>
#include <Oxygen/Data/ShaderReference.h>
#include <Oxygen/Engine/AsyncEngine.h> // IWYU pragma: keep
#include <Oxygen/Host/HostConfig.h>
#include <Oxygen/Host/HostModule.h>
#include <Oxygen/Input/Action.h>
#include <Oxygen/Input/ActionTriggers.h>
#include <Oxygen/Input/ActionValue.h>
#include <Oxygen/Input/CodeInput.h>
#include <Oxygen/Input/InputActionMapping.h>
#include <Oxygen/Input/InputMappingContext.h>
#include <Oxygen/Input/InputSystem.h>
#include <Oxygen/OxCo/Co.h>
#include <Oxygen/Platform/Input.h>
#include <Oxygen/Platform/Window.h>
#include <Oxygen/Scene/Camera/Perspective.h>
#include <Oxygen/Scene/Scene.h>
#include <Oxygen/Vortex/CompositionView.h>

namespace {

//! The owner of the inputs created here in code (see
//! oxygen::input::MakeCodeAction).
constexpr std::string_view kInputOwner = "Examples/InputSystem";

constexpr std::uint32_t kWindowWidth = 2400;
constexpr std::uint32_t kWindowHeight = 1400;
constexpr size_t kDefaultSceneCapacity = 128;
constexpr oxygen::Vec3 kSceneLightingFocusPoint { 0.0F, -4.0F, 1.5F };
constexpr oxygen::Vec3 kSunPosition { -8.0F, -12.0F, 14.0F };

auto MakeSolidColorMaterial(const char* name, const glm::vec4& rgba,
  oxygen::data::MaterialDomain domain = oxygen::data::MaterialDomain::kOpaque)
  -> std::shared_ptr<const oxygen::data::MaterialAsset>
{
  namespace d = oxygen::data;
  namespace pak = oxygen::data::pak;

  pak::render::MaterialAssetDesc desc {};
  desc.header.asset_type
    = static_cast<uint8_t>(oxygen::data::AssetType::kMaterial);
  desc.header.version = oxygen::data::pak::render::kMaterialAssetVersion;
  desc.header.streaming_priority = 255;
  desc.material_domain = static_cast<uint8_t>(domain);
  desc.flags = pak::render::kMaterialFlag_NoTextureSampling;
  desc.shader_stages = 0;
  desc.base_color[0] = rgba.r;
  desc.base_color[1] = rgba.g;
  desc.base_color[2] = rgba.b;
  desc.base_color[3] = rgba.a;
  desc.normal_scale = 1.0F;
  desc.metalness = d::Unorm16 { 0.0F };
  desc.roughness = d::Unorm16 { 0.6F };
  desc.ambient_occlusion = d::Unorm16 { 1.0F };
  const d::AssetKey asset_key = d::AssetKey::FromVirtualPath(
    "/Engine/Examples/InputSystem/Materials/" + std::string(name) + ".omat");
  auto material = std::make_shared<d::MaterialAsset>(
    asset_key, desc, std::vector<d::ShaderReference> {});
  material->SetAssetName(name);
  return material;
}

} // namespace

namespace oxygen::examples::input_system {

MainModule::MainModule(const host::HostContext& host)
  : Base(host)
{
}

auto MainModule::BuildWindowProperties() const -> platform::window::Properties
{
  auto props = Base::BuildWindowProperties();
  props.title = "Oxygen Input System";
  props.extent = { .width = kWindowWidth, .height = kWindowHeight };
  return props;
}

auto MainModule::StageInitialScene() -> void
{
  Stage().Stage(
    std::make_shared<scene::Scene>("InputSystem-Scene", kDefaultSceneCapacity));
  const auto staged_scene = Stage().GetStagedScene();

  main_camera_ = staged_scene->CreateNode("MainCamera");
  auto camera = std::make_unique<scene::PerspectiveCamera>();
  const bool attached = main_camera_.AttachCamera(std::move(camera));
  CHECK_F(attached, "Failed to attach PerspectiveCamera to MainCamera");
  auto tf = main_camera_.GetTransform();
  tf.SetLocalPosition(Vec3 { 0.0F, -6.0F, 3.0F });
  // Face the scene (+Y), pitched 20 degrees down.
  const auto pitch = glm::radians(-20.0F);
  tf.SetLocalRotation(transforms::LookRotation(
    { .forward = Vec3 { 0.0F, std::cos(pitch), std::sin(pitch) } }));
  Stage().SetStagedMainCamera(main_camera_);
  sun_light_node_ = EnsureDefaultSceneLighting(*staged_scene,
    DefaultSceneLightingDesc {
      .sun_node_name = "SunLight",
      .sun_position = kSunPosition,
      .focus_point = kSceneLightingFocusPoint,
    });
}

// Attaching only builds the shell and its bindings; running out of memory there
// is not recoverable.
// NOLINTNEXTLINE(bugprone-exception-escape)
auto MainModule::OnAttachedImpl(
  [[maybe_unused]] observer_ptr<IAsyncEngine> engine) noexcept
  -> std::unique_ptr<DemoShell>
{
  DCHECK_NOTNULL_F(engine, "expecting a valid engine");

  if (!InitInputBindings()) {
    return nullptr;
  }

  auto shell = std::make_unique<DemoShell>();
  const auto demo_root
    = std::filesystem::path(std::source_location::current().file_name())
        .parent_path();
  auto shell_config = MakeShellConfig();
  shell_config.enable_camera_rig = true;
  shell_config.force_environment_override = false;
  shell_config.content_roots = {
    .content_root = demo_root.parent_path() / "Content",
    .cooked_root = demo_root / ".cooked",
  };
  shell_config.panel_config = {
    .content_loader = false,
    .camera_controls = true,
    .environment = true,
    .post_process = true,
    .ground_grid = true,
  };

  if (!shell->Initialize(shell_config)) {
    LOG_F(WARNING, "InputSystem: DemoShell initialization failed");
    return nullptr;
  }

  shell->GetPanelCommands()->AddBool(std::string(kSwimmingCommand),
    "Swimming Mode", "Space rises instead of jumping",
    [this](const bool swimming) -> void { SetSwimmingMode(swimming); });

  input_debug_panel_ = std::make_shared<InputDebugPanel>();
  input_debug_panel_->Initialize(InputDebugPanelConfig {
    .camera_rig = shell->GetCameraRig(),
    .commands = shell->GetPanelCommands(),
    .shift_action = shift_action_,
    .jump_action = jump_action_,
    .jump_higher_action = jump_higher_action_,
    .swim_up_action = swim_up_action_,
    .swimming_mode = observer_ptr<const bool> { &swimming_mode_ },
  });
  if (!shell->RegisterPanel(input_debug_panel_)) {
    LOG_F(WARNING, "InputSystem: failed to register Input Debug panel");
    return nullptr;
  }

  StageInitialScene();

  main_view_id_ = Publisher().GetOrCreateViewId("MainView");
  LOG_F(INFO, "InputSystem: Module initialized");
  return shell;
}

void MainModule::OnShutdown() noexcept
{
  main_view_state_.Reset({}, ResolveRenderer(), main_view_id_);
  Stage().Clear();
  Base::OnShutdown();
  input_debug_panel_.reset();
}

auto MainModule::OnScenePublished(engine::FrameContext& context) -> void
{
  Base::OnScenePublished(context);
  active_scene_ = Stage().GetActiveScene();
  main_camera_ = Stage().TakePublishedMainCamera();
}

auto MainModule::OnGameplay(observer_ptr<engine::FrameContext> context)
  -> co::Co<>
{
  namespace c = std::chrono;
  const auto u_game_dt = context->GetGameDeltaTime().get();
  const float dt = c::duration<float>(u_game_dt).count();

  if (pending_ground_reset_) {
    pending_ground_reset_ = false;
    if (sphere_node_.IsAlive()) {
      auto tf = sphere_node_.GetTransform();
      tf.SetLocalPosition(sphere_base_pos_);
    }
    sphere_vel_z_ = 0.0F;
    sphere_in_air_ = false;
  }

  if (swimming_mode_) {
    if (swim_up_action_ && swim_up_action_->IsOngoing()) {
      if (sphere_node_.IsAlive()) {
        auto tf = sphere_node_.GetTransform();
        auto pos = tf.GetLocalPosition().value_or(sphere_base_pos_);
        pos.z += swim_up_speed_ * dt;
        tf.SetLocalPosition(pos);
      }
    }
    sphere_in_air_ = false;
    sphere_vel_z_ = 0.0F;
  } else {
    if (jump_higher_action_ && jump_higher_action_->WasTriggeredThisFrame()) {
      if (!sphere_in_air_) {
        sphere_in_air_ = true;
        sphere_vel_z_ = jump_higher_impulse_;
      }
    } else if (jump_action_ && jump_action_->WasTriggeredThisFrame()) {
      if (!sphere_in_air_) {
        sphere_in_air_ = true;
        sphere_vel_z_ = jump_impulse_;
      }
    }
  }

  if (!swimming_mode_ && sphere_in_air_ && sphere_node_.IsAlive()) {
    sphere_vel_z_ += gravity_ * dt;
    auto tf = sphere_node_.GetTransform();
    auto pos = tf.GetLocalPosition().value_or(sphere_base_pos_);
    pos.z += sphere_vel_z_ * dt;
    if (pos.z <= sphere_base_pos_.z) {
      pos.z = sphere_base_pos_.z;
      sphere_vel_z_ = 0.0F;
      sphere_in_air_ = false;
    }
    tf.SetLocalPosition(pos);
  }
  co_return;
}

auto MainModule::OnSceneMutation(observer_ptr<engine::FrameContext> context)
  -> co::Co<>
{
  if (!HasRenderableWindow() || !active_scene_.IsValid()) {
    co_await Base::OnSceneMutation(context);
    co_return;
  }

  const auto scene_ptr = Stage().TryGetScene();
  if (!scene_ptr) {
    co_return;
  }
  auto* scene = scene_ptr.get();

  using oxygen::data::MeshBuilder;
  using oxygen::data::pak::geometry::GeometryAssetDesc;
  using oxygen::data::pak::geometry::MeshViewDesc;

  if (!std::ranges::any_of(scene->GetRootNodes(),
        [](auto& n) -> auto { return n.GetName() == "Sphere"; })) {
    auto sphere_data = oxygen::data::MakeSphereMeshAsset(24, 48);
    if (sphere_data) {
      const auto sphere_mat
        = MakeSolidColorMaterial("SphereMat", { 0.85F, 0.2F, 0.2F, 1.0F });
      auto mesh
        = MeshBuilder(oxygen::data::LodIndex {}, "SphereLOD0")
            .WithVertices(sphere_data->first)
            .WithIndices(sphere_data->second)
            .BeginSubMesh("full", sphere_mat)
            .WithMeshView(MeshViewDesc {
              .first_index = 0,
              .index_count = static_cast<uint32_t>(sphere_data->second.size()),
              .first_vertex = 0,
              .vertex_count = static_cast<uint32_t>(sphere_data->first.size()),
            })
            .EndSubMesh()
            .Build();

      GeometryAssetDesc geo_desc {};
      geo_desc.lod_count = 1;
      const auto bb_min = mesh->BoundingBoxMin();
      const auto bb_max = mesh->BoundingBoxMax();
      geo_desc.bounding_box_min[0] = bb_min.x;
      geo_desc.bounding_box_min[1] = bb_min.y;
      geo_desc.bounding_box_min[2] = bb_min.z;
      geo_desc.bounding_box_max[0] = bb_max.x;
      geo_desc.bounding_box_max[1] = bb_max.y;
      geo_desc.bounding_box_max[2] = bb_max.z;

      auto sphere_geo = std::make_shared<oxygen::data::GeometryAsset>(
        oxygen::data::AssetKey::FromVirtualPath(
          "/Engine/Examples/InputSystem/Geometry/Sphere.ogeo"),
        geo_desc,
        std::vector<std::shared_ptr<oxygen::data::Mesh>> { std::move(mesh) });

      sphere_node_ = scene->CreateNode("Sphere");
      sphere_node_.GetRenderable().SetGeometry(std::move(sphere_geo));
      sphere_node_.GetTransform().SetLocalPosition(sphere_base_pos_);
    }
  }

  co_await Base::OnSceneMutation(context);
}

auto MainModule::InitInputBindings() noexcept -> bool
{
  using oxygen::input::Action;
  using oxygen::input::ActionTriggerDown;
  using oxygen::input::ActionTriggerTap;
  using oxygen::input::ActionValueType;
  using oxygen::input::InputActionMapping;
  using oxygen::input::InputMappingContext;
  using platform::InputSlots;

  if (!Host().input_system) {
    LOG_F(ERROR, "InputSystem not available; skipping input bindings");
    return false;
  }

  shift_action_ = oxygen::input::MakeCodeAction(
    kInputOwner, "shift", ActionValueType::kBool);
  Host().input_system->AddAction(shift_action_);
  modifier_keys_ctx_
    = oxygen::input::MakeCodeContext(kInputOwner, "modifier keys");
  {
    const auto trigger = std::make_shared<oxygen::input::ActionTriggerDown>();
    trigger->MakeExplicit();
    const auto left_shift_mapping = std::make_shared<InputActionMapping>(
      shift_action_, InputSlots::LeftShift);
    left_shift_mapping->AddTrigger(trigger);
    modifier_keys_ctx_->AddMapping(left_shift_mapping);
  }
  Host().input_system->AddMappingContext(modifier_keys_ctx_, 1000);

  jump_action_ = oxygen::input::MakeCodeAction(
    kInputOwner, "jump", ActionValueType::kBool);
  Host().input_system->AddAction(jump_action_);

  jump_higher_action_ = oxygen::input::MakeCodeAction(
    kInputOwner, "jump higher", ActionValueType::kBool);
  jump_higher_action_->SetConsumesInput(true);
  Host().input_system->AddAction(jump_higher_action_);

  swim_up_action_ = oxygen::input::MakeCodeAction(
    kInputOwner, "swim up", ActionValueType::kBool);
  Host().input_system->AddAction(swim_up_action_);

  ground_movement_ctx_
    = oxygen::input::MakeCodeContext(kInputOwner, "ground movement");
  {
    {
      const auto trigger = std::make_shared<ActionTriggerTap>();
      trigger->SetTapTimeThreshold(0.25F);
      trigger->MakeExplicit();
      const auto mapping = std::make_shared<InputActionMapping>(
        jump_higher_action_, InputSlots::Space);
      mapping->AddTrigger(trigger);

      const auto shift_trigger
        = std::make_shared<oxygen::input::ActionTriggerChain>();
      shift_trigger->SetLinkedAction(shift_action_);
      shift_trigger->MakeImplicit();
      mapping->AddTrigger(shift_trigger);
      ground_movement_ctx_->AddMapping(mapping);
    }
    {
      const auto trigger = std::make_shared<ActionTriggerTap>();
      trigger->SetTapTimeThreshold(0.25F);
      trigger->MakeExplicit();
      const auto mapping
        = std::make_shared<InputActionMapping>(jump_action_, InputSlots::Space);
      mapping->AddTrigger(trigger);
      ground_movement_ctx_->AddMapping(mapping);
    }
    Host().input_system->AddMappingContext(ground_movement_ctx_, 0);
  }

  swimming_ctx_ = oxygen::input::MakeCodeContext(kInputOwner, "swimming");
  {
    auto trig_down = std::make_shared<ActionTriggerDown>();
    trig_down->MakeExplicit();
    trig_down->SetActuationThreshold(0.1F);
    auto mapping = std::make_shared<InputActionMapping>(
      swim_up_action_, InputSlots::Space);
    mapping->AddTrigger(trig_down);
    swimming_ctx_->AddMapping(mapping);
    Host().input_system->AddMappingContext(swimming_ctx_, 0);
  }

  Host().input_system->ActivateMappingContext(modifier_keys_ctx_);
  Host().input_system->ActivateMappingContext(ground_movement_ctx_);
  return true;
}

auto MainModule::SetSwimmingMode(const bool swimming) -> void
{
  if (swimming == swimming_mode_) {
    return;
  }
  swimming_mode_ = swimming;
  const auto input = Host().input_system;
  if (swimming) {
    input->DeactivateMappingContext(ground_movement_ctx_);
    input->ActivateMappingContext(swimming_ctx_);
  } else {
    input->DeactivateMappingContext(swimming_ctx_);
    input->ActivateMappingContext(ground_movement_ctx_);
    pending_ground_reset_ = true;
  }
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

} // namespace oxygen::examples::input_system
