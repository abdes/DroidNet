//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <memory>
#include <numbers>
#include <source_location>
#include <string>
#include <utility>

#include <glm/gtc/quaternion.hpp>
#include <imgui.h>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Core/Constants.h>
#include <Oxygen/Core/FrameContext.h>
#include <Oxygen/Core/Types/ViewPort.h>
#include <Oxygen/Data/AssetKey.h>
#include <Oxygen/Data/GeometryAsset.h>
#include <Oxygen/Data/MaterialAsset.h>
#include <Oxygen/Data/PakFormat.h>
#include <Oxygen/Data/ProceduralMeshes.h>
#include <Oxygen/Data/ShaderReference.h>
#include <Oxygen/Engine/AsyncEngine.h>
#include <Oxygen/Input/Action.h>
#include <Oxygen/Input/ActionTriggers.h>
#include <Oxygen/Input/InputActionMapping.h>
#include <Oxygen/Input/InputMappingContext.h>
#include <Oxygen/Input/InputSystem.h>
#include <Oxygen/OxCo/Co.h>
#include <Oxygen/Physics/Body/BodyDesc.h>
#include <Oxygen/Physics/Shape.h>
#include <Oxygen/PhysicsModule/PhysicsModule.h>
#include <Oxygen/Platform/Input.h>
#include <Oxygen/Platform/Window.h>
#include <Oxygen/Scene/Camera/Perspective.h>
#include <Oxygen/Scene/Scene.h>
#include <Oxygen/Scene/SceneFlags.h>
#include <Oxygen/Scene/Types/Flags.h>
#include <Oxygen/Vortex/CompositionView.h>

#include "DemoShell/Runtime/DemoAppContext.h"
#include "DemoShell/Services/DefaultSceneLighting.h"
#include "Physics/MainModule.h"

namespace {

constexpr std::uint32_t kWindowWidth = 2400;
constexpr std::uint32_t kWindowHeight = 1400;
constexpr float kRampPitchRad = -0.44F;
constexpr oxygen::Vec3 kFloorCollisionSize { 32.0F, 32.0F, 1.0F };
constexpr oxygen::Vec3 kFloorCollisionCenterWs { 0.0F, 0.0F, -0.5F };
constexpr float kFloorCollisionTopZ
  = kFloorCollisionCenterWs.z + 0.5F * kFloorCollisionSize.z;
// Oxygen is +Z up. Keep the visible floor just above the UE-style atmosphere
// planet-top boundary at Z=0 so per-pixel sun transmittance is not occluded.
constexpr float kFloorVisualClearanceAboveAtmosphereM = 0.02F;
constexpr oxygen::Vec3 kFloorVisualCenterWs {
  kFloorCollisionCenterWs.x,
  kFloorCollisionCenterWs.y,
  kFloorCollisionTopZ + kFloorVisualClearanceAboveAtmosphereM
};
constexpr oxygen::Vec3 kFloorVisualScale {
  kFloorCollisionSize.x,
  kFloorCollisionSize.y,
  1.0F
};
constexpr oxygen::Vec3 kRampPosition { 0.0F, -6.5F, 4.6F };
constexpr oxygen::Vec3 kRampScale { 3.6F, 15.0F, 0.05F };
constexpr oxygen::Vec3 kRampRailScale { 0.35F, 15.0F, 0.8F };
constexpr oxygen::Vec3 kSceneLightingFocusPoint { 0.0F, -3.5F, 2.8F };
constexpr oxygen::Vec3 kSunPosition { -14.0F, -18.0F, 22.0F };
constexpr float kPlayerSphereRadius = 0.5F;
constexpr float kSpawnSurfaceClearance = 0.05F;
constexpr float kBowlRingSphereRadius = 0.5F;

auto MakeSolidColorMaterial(const char* name, const glm::vec4& rgba,
  oxygen::data::MaterialDomain domain = oxygen::data::MaterialDomain::kOpaque)
  -> std::shared_ptr<const oxygen::data::MaterialAsset>
{
  namespace d = oxygen::data;
  namespace pak = oxygen::data::pak;

  pak::render::MaterialAssetDesc desc {};
  desc.header.asset_type
    = static_cast<uint8_t>(oxygen::data::AssetType::kMaterial);
  constexpr std::size_t maxn = sizeof(desc.header.name) - 1;
  const std::size_t n = (std::min)(maxn, std::strlen(name));
  std::memcpy(desc.header.name, name, n);
  desc.header.name[n] = '\0';
  desc.header.version = 1;
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
    "/Engine/Examples/Physics/Materials/" + std::string(name) + ".omat");
  return std::make_shared<const d::MaterialAsset>(
    asset_key, desc, std::vector<d::ShaderReference> {});
}

auto BuildGeometryAsset(const char* mesh_name,
  const std::pair<std::vector<oxygen::data::Vertex>, std::vector<uint32_t>>&
    mesh_data,
  const std::shared_ptr<const oxygen::data::MaterialAsset>& material)
  -> std::shared_ptr<oxygen::data::GeometryAsset>
{
  using oxygen::data::MeshBuilder;
  using oxygen::data::pak::geometry::GeometryAssetDesc;
  using oxygen::data::pak::geometry::MeshViewDesc;

  auto mesh = MeshBuilder(0, mesh_name)
                .WithVertices(mesh_data.first)
                .WithIndices(mesh_data.second)
                .BeginSubMesh("full", material)
                .WithMeshView(MeshViewDesc {
                  .first_index = 0,
                  .index_count = static_cast<uint32_t>(mesh_data.second.size()),
                  .first_vertex = 0,
                  .vertex_count = static_cast<uint32_t>(mesh_data.first.size()),
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

  return std::make_shared<oxygen::data::GeometryAsset>(
    oxygen::data::AssetKey::FromVirtualPath(
      "/Engine/Examples/Physics/Geometry/" + std::string(mesh_name) + ".ogeo"),
    geo_desc,
    std::vector<std::shared_ptr<oxygen::data::Mesh>> { std::move(mesh) });
}

auto MakeXRotationQuat(const float radians) -> glm::quat
{
  return glm::angleAxis(radians, glm::vec3 { 1.0F, 0.0F, 0.0F });
}

auto MakeZRotationQuat(const float radians) -> glm::quat
{
  return glm::angleAxis(radians, glm::vec3 { 0.0F, 0.0F, 1.0F });
}

void SetShadowParticipation(oxygen::scene::SceneNode& node,
  const bool casts_shadows, const bool receives_shadows)
{
  if (auto flags_ref = node.GetFlags(); flags_ref.has_value()) {
    auto& flags = flags_ref->get();
    flags = flags.SetFlag(oxygen::scene::SceneNodeFlags::kCastsShadows,
      oxygen::scene::SceneFlag {}.SetEffectiveValueBit(casts_shadows));
    flags = flags.SetFlag(oxygen::scene::SceneNodeFlags::kReceivesShadows,
      oxygen::scene::SceneFlag {}.SetEffectiveValueBit(receives_shadows));
  }
}

} // namespace

namespace oxygen::examples::physics_demo {

MainModule::MainModule(const DemoAppContext& app)
  : Base(app)
{
  DCHECK_NOTNULL_F(app_.platform);
  DCHECK_F(!app_.gfx_weak.expired());
}

auto MainModule::BuildDefaultWindowProperties() const
  -> platform::window::Properties
{
  platform::window::Properties p("Oxygen Physics");
  p.extent = { .width = kWindowWidth, .height = kWindowHeight };
  p.flags = { .hidden = false,
    .always_on_top = false,
    .full_screen = app_.fullscreen,
    .maximized = false,
    .minimized = false,
    .resizable = true,
    .borderless = false };
  return p;
}

auto MainModule::ClearBackbufferReferences() -> void { }

auto MainModule::OnAttachedImpl(
  oxygen::observer_ptr<oxygen::IAsyncEngine> engine) noexcept
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
  DemoShellConfig shell_config {
    .engine = observer_ptr { app_.engine.get() },
    .enable_camera_rig = true,
    .force_environment_override = false,
    .content_roots = {
      .content_root = demo_root.parent_path() / "Content",
      .cooked_root = demo_root / ".cooked",
    },
    .panel_config = {
      .content_loader = false,
      .camera_controls = true,
      .environment = true,
      .lighting = false,
      .diagnostics = true,
      .post_process = true,
      .ground_grid = true,
    },
  };

  if (!shell->Initialize(shell_config)) {
    LOG_F(WARNING, "Physics: DemoShell initialization failed");
    return nullptr;
  }

  physics_panel_ = std::make_shared<PhysicsDemoPanel>();
  UpdatePhysicsDemoPanelConfig(shell->GetCameraRig());
  if (!shell->RegisterPanel(physics_panel_)) {
    LOG_F(WARNING, "Physics: failed to register panel");
    return nullptr;
  }

  shell->StageScene(std::make_unique<scene::Scene>("Physics-Scene", 512));
  const auto staged_scene = shell->GetStagedScene();
  CHECK_NOTNULL_F(staged_scene, "Physics staged scene is null");
  auto camera_node = staged_scene->CreateNode("MainCamera");
  auto camera = std::make_unique<scene::PerspectiveCamera>();
  CHECK_F(camera_node.AttachCamera(std::move(camera)),
    "Failed to attach PerspectiveCamera to MainCamera");
  auto tf = camera_node.GetTransform();
  tf.SetLocalPosition(Vec3 { 0.0F, -26.0F, 13.0F });
  tf.SetLocalRotation(glm::quat(glm::radians(Vec3 { -24.0F, 0.0F, 0.0F })));
  shell->SetStagedMainCamera(camera_node);
  sun_light_node_ = EnsureDefaultSceneLighting(*staged_scene,
    DefaultSceneLightingDesc {
      .sun_node_name = "SunLight",
      .sun_position = kSunPosition,
      .focus_point = kSceneLightingFocusPoint,
    });

  main_view_id_ = GetOrCreateViewId("MainView");
  LOG_F(INFO, "Physics: Module initialized");
  return shell;
}

void MainModule::OnShutdown() noexcept
{
  auto& shell = GetShell();
  shell.SetScene(nullptr);

  sun_light_node_ = {};
  physics_panel_.reset();
  gameplay_input_ctx_.reset();
  launch_action_.reset();
  reset_action_.reset();
  nudge_left_action_.reset();
  nudge_right_action_.reset();

  Base::OnShutdown();
}

auto MainModule::OnFrameStart(observer_ptr<engine::FrameContext> context)
  -> void
{
  DCHECK_NOTNULL_F(context);
  auto& shell = GetShell();

  if (shell.HasStagedScene()) {
    CHECK_F(shell.PublishStagedScene(),
      "expected staged scene before frame-start publish");
    active_scene_ = shell.GetActiveScene();
    main_camera_ = shell.TakePublishedMainCamera();
  }

  shell.OnFrameStart(*context);
  Base::OnFrameStart(context);

  if (!HasRenderableWindow()) {
    return;
  }

  context->SetScene(shell.TryGetScene());

  const auto rig = shell.GetCameraRig();
  if (rig != last_camera_rig_) {
    last_camera_rig_ = rig;
  }
}

auto MainModule::OnFrameEnd(observer_ptr<engine::FrameContext> /*context*/)
  -> void
{
}

auto MainModule::OnPreRender(observer_ptr<engine::FrameContext> context)
  -> co::Co<>
{
  DCHECK_NOTNULL_F(app_window_);

  if (!app_window_->GetWindow()) {
    co_return;
  }

  co_await Base::OnPreRender(context);
}

auto MainModule::OnSceneMutation(observer_ptr<engine::FrameContext> context)
  -> co::Co<>
{
  DCHECK_NOTNULL_F(app_window_);
  auto& shell = GetShell();

  if (!app_window_->GetWindow()) {
    co_return;
  }

  if (!active_scene_.IsValid() && !shell.HasStagedScene()) {
    if (!StageScenarioScene()) {
      ReportError(context, "failed to stage initial scenario");
    }
  }

  if (!scene_ready_ && !BuildProceduralScene()) {
    ReportError(context, "failed to build procedural scene");
    co_return;
  }

  if (!active_scene_.IsValid()) {
    co_await Base::OnSceneMutation(context);
    co_return;
  }

  if (scene_ready_ && !physics_ready_) {
    (void)InitializePhysicsScenario();
  }

  co_await Base::OnSceneMutation(context);
}

auto MainModule::OnGameplay(observer_ptr<engine::FrameContext> context)
  -> co::Co<>
{
  namespace c = std::chrono;
  const auto dt = c::duration<float>(context->GetGameDeltaTime().get()).count();
  auto& shell = GetShell();

  if (launch_action_ && launch_action_->WasTriggeredThisFrame()) {
    pending_launch_ = true;
  }
  if (reset_action_ && reset_action_->WasTriggeredThisFrame()) {
    pending_reset_ = true;
  }

  if (scene_ready_ && !physics_ready_) {
    (void)InitializePhysicsScenario();
  }

  if (physics_ready_) {
    if (pending_reset_) {
      pending_reset_ = false;
      ResetScenario();
      shell.Update(context->GetGameDeltaTime());
      co_return;
    }

    if (pending_launch_) {
      pending_launch_ = false;
      LaunchSphere();
    }

    if (player_body_) {
      auto* module = ResolvePhysicsModule().get();
      if (module != nullptr) {
        const auto world_id = module->GetWorldId();
        const auto body_id = *player_body_;
        if (nudge_left_action_ && nudge_left_action_->WasTriggeredThisFrame()) {
          constexpr float kNudgeVelocityDelta = 6.0F;
          const auto current_velocity
            = module->Bodies().GetLinearVelocity(world_id, body_id);
          if (!current_velocity.has_value()) {
            LOG_F(ERROR, "Physics: left nudge GetLinearVelocity failed");
          } else {
            const auto target_velocity = current_velocity.value()
              - space::move::Right * kNudgeVelocityDelta;
            const auto result = module->Bodies().SetLinearVelocity(
              world_id, body_id, target_velocity);
            if (!result.has_value()) {
              LOG_F(ERROR, "Physics: left nudge SetLinearVelocity failed");
            }
          }
        }
        if (nudge_right_action_
          && nudge_right_action_->WasTriggeredThisFrame()) {
          constexpr float kNudgeVelocityDelta = 6.0F;
          const auto current_velocity
            = module->Bodies().GetLinearVelocity(world_id, body_id);
          if (!current_velocity.has_value()) {
            LOG_F(ERROR, "Physics: right nudge GetLinearVelocity failed");
          } else {
            const auto target_velocity = current_velocity.value()
              + space::move::Right * kNudgeVelocityDelta;
            const auto result = module->Bodies().SetLinearVelocity(
              world_id, body_id, target_velocity);
            if (!result.has_value()) {
              LOG_F(ERROR, "Physics: right nudge SetLinearVelocity failed");
            }
          }
        }
      }
    }

    UpdateFlippers(dt);

    if (player_body_) {
      auto* module = ResolvePhysicsModule().get();
      if (module != nullptr) {
        const auto world_id = module->GetWorldId();
        const auto player_pos
          = module->Bodies().GetBodyPosition(world_id, *player_body_);
        if (player_pos.has_value()) {
          const auto& pos = player_pos.value();
          if (previous_player_world_position_ && dt > 0.0F) {
            player_speed_
              = glm::distance(pos, *previous_player_world_position_) / dt;
          }
          previous_player_world_position_ = pos;

          const bool in_bowl_zone = glm::distance(pos, bowl_center_) < 2.5F;
          if (in_bowl_zone && player_speed_ < kSettleSpeedThreshold) {
            settle_timer_sec_ += dt;
          } else {
            settle_timer_sec_ = 0.0F;
          }
          player_settled_ = settle_timer_sec_ >= kSettleTimeRequiredSec;

          constexpr float kSettleDisplayDeadzoneSec = 0.06F;
          float settle_display_target = settle_timer_sec_;
          if (settle_display_target < kSettleDisplayDeadzoneSec) {
            settle_display_target = 0.0F;
          }
          const float smoothing_rate
            = settle_display_target > settle_display_sec_ ? 10.0F : 16.0F;
          const float alpha = 1.0F - std::exp(-smoothing_rate * dt);
          settle_display_sec_
            += (settle_display_target - settle_display_sec_) * alpha;
        }
      }
    }
  }

  shell.Update(context->GetGameDeltaTime());
  co_return;
}

auto MainModule::OnGuiUpdate(observer_ptr<engine::FrameContext> context)
  -> co::Co<>
{
  DCHECK_NOTNULL_F(app_window_);
  auto& shell = GetShell();

  if (app_window_->IsShuttingDown()) {
    co_return;
  }

  static bool camera_rig_bound = false;
  if (!camera_rig_bound && shell.GetCameraRig()) {
    UpdatePhysicsDemoPanelConfig(shell.GetCameraRig());
    camera_rig_bound = true;
  }

  shell.Draw(context);

  co_return;
}

auto MainModule::InitInputBindings() noexcept -> bool
{
  using oxygen::input::Action;
  using oxygen::input::ActionTriggerPressed;
  using oxygen::input::ActionValueType;
  using oxygen::input::InputActionMapping;
  using oxygen::input::InputMappingContext;
  using platform::InputSlots;

  if (!app_.input_system) {
    LOG_F(ERROR, "InputSystem not available; skipping input bindings");
    return false;
  }

  launch_action_ = std::make_shared<Action>("launch", ActionValueType::kBool);
  reset_action_ = std::make_shared<Action>("reset", ActionValueType::kBool);
  nudge_left_action_
    = std::make_shared<Action>("nudge left", ActionValueType::kBool);
  nudge_right_action_
    = std::make_shared<Action>("nudge right", ActionValueType::kBool);

  app_.input_system->AddAction(launch_action_);
  app_.input_system->AddAction(reset_action_);
  app_.input_system->AddAction(nudge_left_action_);
  app_.input_system->AddAction(nudge_right_action_);

  gameplay_input_ctx_
    = std::make_shared<InputMappingContext>("physics gameplay");

  {
    auto pressed = std::make_shared<ActionTriggerPressed>();
    pressed->MakeExplicit();
    auto mapping = std::make_shared<InputActionMapping>(
      launch_action_, InputSlots::UpArrow);
    mapping->AddTrigger(pressed);
    gameplay_input_ctx_->AddMapping(mapping);
  }
  {
    auto pressed = std::make_shared<ActionTriggerPressed>();
    pressed->MakeExplicit();
    auto mapping
      = std::make_shared<InputActionMapping>(reset_action_, InputSlots::G);
    mapping->AddTrigger(pressed);
    gameplay_input_ctx_->AddMapping(mapping);
  }
  {
    auto pressed = std::make_shared<ActionTriggerPressed>();
    pressed->MakeExplicit();
    auto mapping = std::make_shared<InputActionMapping>(
      nudge_left_action_, InputSlots::LeftArrow);
    mapping->AddTrigger(pressed);
    gameplay_input_ctx_->AddMapping(mapping);
  }
  {
    auto pressed = std::make_shared<ActionTriggerPressed>();
    pressed->MakeExplicit();
    auto mapping = std::make_shared<InputActionMapping>(
      nudge_right_action_, InputSlots::RightArrow);
    mapping->AddTrigger(pressed);
    gameplay_input_ctx_->AddMapping(mapping);
  }

  app_.input_system->AddMappingContext(gameplay_input_ctx_, 0);
  app_.input_system->ActivateMappingContext(gameplay_input_ctx_);

  return true;
}

auto MainModule::ResolvePhysicsModule() -> observer_ptr<physics::PhysicsModule>
{
  if (!app_.engine) {
    return nullptr;
  }
  if (auto module_ref = app_.engine->GetModule<physics::PhysicsModule>()) {
    return observer_ptr<physics::PhysicsModule> { &module_ref->get() };
  }
  return nullptr;
}

auto MainModule::SpawnRenderableNode(std::string_view name,
  const std::shared_ptr<data::GeometryAsset>& geometry, const Vec3& position,
  const glm::quat& rotation, const Vec3& scale) -> scene::SceneNode
{
  auto scene_ptr = GetShell().TryGetScene();
  CHECK_NOTNULL_F(scene_ptr, "scene is required to spawn nodes");

  auto node = scene_ptr->CreateNode(std::string(name));
  node.GetRenderable().SetGeometry(geometry);
  SetShadowParticipation(node, name != "Floor", true);
  auto tf = node.GetTransform();
  tf.SetLocalPosition(position);
  tf.SetLocalRotation(rotation);
  tf.SetLocalScale(scale);
  return node;
}

auto MainModule::AttachRigidBody(scene::SceneNode& node,
  const physics::body::BodyDesc& desc) -> std::optional<physics::BodyId>
{
  auto* module = ResolvePhysicsModule().get();
  if (module == nullptr) {
    return std::nullopt;
  }
  return physics::ScenePhysics::AttachRigidBody(
    observer_ptr<physics::PhysicsModule> { module }, node, desc);
}

auto MainModule::BuildProceduralScene() -> bool
{
  auto scene_ptr = GetShell().TryGetScene();
  if (!scene_ptr) {
    return false;
  }

  if (!cube_geometry_ || !floor_geometry_ || !sphere_geometry_
    || !player_sphere_geometry_) {
    const auto cube_mesh = data::MakeCubeMeshAsset();
    const auto floor_mesh = data::MakePlaneMeshAsset(8, 8, 1.0F);
    const auto sphere_mesh = data::MakeSphereMeshAsset(24, 48);
    if (!cube_mesh || !floor_mesh || !sphere_mesh) {
      return false;
    }

    const auto cube_mat
      = MakeSolidColorMaterial("PhysicsCubeMat", { 0.78F, 0.80F, 0.88F, 1.0F });
    const auto floor_mat = MakeSolidColorMaterial(
      "PhysicsFloorMat", { 0.44F, 0.47F, 0.50F, 1.0F });
    const auto sphere_mat = MakeSolidColorMaterial(
      "PhysicsSphereMat", { 0.94F, 0.27F, 0.22F, 1.0F });
    const auto player_sphere_mat = MakeSolidColorMaterial(
      "PhysicsPlayerSphereMat", { 0.19F, 0.81F, 0.31F, 1.0F });

    cube_geometry_ = BuildGeometryAsset("CubeLOD0", *cube_mesh, cube_mat);
    floor_geometry_
      = BuildGeometryAsset("FloorPlaneLOD0", *floor_mesh, floor_mat);
    sphere_geometry_
      = BuildGeometryAsset("SphereLOD0", *sphere_mesh, sphere_mat);
    player_sphere_geometry_
      = BuildGeometryAsset("PlayerSphereLOD0", *sphere_mesh, player_sphere_mat);
  }

  static_nodes_.clear();
  dynamic_obstacles_.clear();
  flippers_.clear();
  player_body_.reset();

  const auto floor_node = SpawnRenderableNode("Floor", floor_geometry_,
    kFloorVisualCenterWs, glm::quat { 1.0F, 0.0F, 0.0F, 0.0F },
    kFloorVisualScale);
  static_nodes_.push_back(floor_node);

  const auto ramp_rotation = MakeXRotationQuat(kRampPitchRad);
  const Vec3 ramp_right = glm::normalize(ramp_rotation * space::move::Right);
  const Vec3 ramp_up = glm::normalize(ramp_rotation * space::move::Up);
  const Vec3 ramp_forward
    = glm::normalize(ramp_rotation * space::move::Forward);
  const Vec3 ramp_half = 0.5F * kRampScale;
  const Vec3 rail_half = 0.5F * kRampRailScale;

  const auto ramp_node = SpawnRenderableNode(
    "Ramp", cube_geometry_, kRampPosition, ramp_rotation, kRampScale);
  static_nodes_.push_back(ramp_node);

  const float rail_side_offset = ramp_half.x + rail_half.x;
  const float rail_height_offset = ramp_half.z + rail_half.z;
  const Vec3 rail_base_offset = ramp_up * rail_height_offset;

  const Vec3 left_rail_position
    = kRampPosition - ramp_right * rail_side_offset + rail_base_offset;
  const Vec3 right_rail_position
    = kRampPosition + ramp_right * rail_side_offset + rail_base_offset;

  static_nodes_.push_back(SpawnRenderableNode("RampWallLeft", cube_geometry_,
    left_rail_position, ramp_rotation, kRampRailScale));
  static_nodes_.push_back(SpawnRenderableNode("RampWallRight", cube_geometry_,
    right_rail_position, ramp_rotation, kRampRailScale));

  constexpr Vec3 kFlipperScale { 1.8F, 0.35F, 0.35F };
  const Vec3 flipper_half = 0.5F * kFlipperScale;
  const float flipper_surface_offset = ramp_half.z + flipper_half.z + 0.01F;
  const auto make_flipper_position
    = [&](const float along, const float side) -> Vec3 {
    return kRampPosition + ramp_forward * along + ramp_right * side
      + ramp_up * flipper_surface_offset;
  };

  const float spawn_along_ramp = ramp_half.y - 2.0F * kPlayerSphereRadius;
  const float spawn_above_ramp
    = ramp_half.z + kPlayerSphereRadius + kSpawnSurfaceClearance;
  player_spawn_position_ = kRampPosition + ramp_forward * spawn_along_ramp
    + ramp_up * spawn_above_ramp;

  player_node_ = SpawnRenderableNode("PlayerSphere", player_sphere_geometry_,
    player_spawn_position_, player_spawn_rotation_, Vec3 { 1.0F, 1.0F, 1.0F });

  for (int row = 0; row < 8; ++row) {
    const float row_t = static_cast<float>(row) / 7.0F;
    const float y = -14.0F + row_t * 14.0F;
    const float z = 8.0F - row_t * 5.2F;
    const bool left_first = (row % 2) == 0;

    for (int c = 0; c < 2; ++c) {
      const float base_x = (c == 0 ? -1.35F : 1.35F);
      const float x = (left_first ? base_x : -base_x);
      auto node = SpawnRenderableNode("ObstacleCube", cube_geometry_,
        Vec3 { x, y, z + 0.65F }, glm::quat { 1.0F, 0.0F, 0.0F, 0.0F },
        Vec3 { 1.1F, 1.1F, 1.1F });

      const bool dynamic = (row % 3) != 0 || c == 1;
      if (dynamic) {
        dynamic_obstacles_.push_back(DynamicObstacleState {
          .node = node,
          .spawn_position = Vec3 { x, y, z + 0.65F },
          .spawn_rotation = Quat { 1.0F, 0.0F, 0.0F, 0.0F },
        });
      } else {
        static_nodes_.push_back(node);
      }
    }
  }

  flippers_.push_back(FlipperState {
    .node = SpawnRenderableNode("FlipperLeft", cube_geometry_,
      make_flipper_position(-1.8F, -1.05F),
      ramp_rotation * MakeZRotationQuat(0.35F), kFlipperScale),
    .position = make_flipper_position(-1.8F, -1.05F),
    .rest_angle_rad = 0.35F,
    .max_swing_rad = 0.95F,
    .direction_sign = 1.0F,
  });
  flippers_.push_back(FlipperState {
    .node = SpawnRenderableNode("FlipperRight", cube_geometry_,
      make_flipper_position(1.9F, 1.05F),
      ramp_rotation * MakeZRotationQuat(-0.35F), kFlipperScale),
    .position = make_flipper_position(1.9F, 1.05F),
    .rest_angle_rad = -0.35F,
    .max_swing_rad = 0.95F,
    .direction_sign = -1.0F,
  });

  constexpr float bowl_ring_center_z
    = kFloorCollisionTopZ + kBowlRingSphereRadius;
  constexpr int ring_count = 14;
  for (int i = 0; i < ring_count; ++i) {
    // Leave an entry gap on the incoming (south) side so the player can enter.
    if (i == 10 || i == 11) {
      continue;
    }
    const float t = static_cast<float>(i) / static_cast<float>(ring_count);
    const float angle = t * (2.0F * std::numbers::pi_v<float>);
    const Vec3 p {
      bowl_center_.x + std::cos(angle) * 2.25F,
      bowl_center_.y + std::sin(angle) * 2.25F,
      bowl_ring_center_z,
    };

    static_nodes_.push_back(SpawnRenderableNode("BowlRing", sphere_geometry_, p,
      glm::quat { 1.0F, 0.0F, 0.0F, 0.0F }, Vec3 { 1.0F, 1.0F, 1.0F }));
  }

  scene_ready_ = true;
  pending_reset_ = true;
  return true;
}

auto MainModule::InitializePhysicsScenario() -> bool
{
  auto* module = ResolvePhysicsModule().get();
  if (!scene_ready_ || physics_ready_) {
    return false;
  }
  DCHECK_NOTNULL_F(module,
    "Physics demo contract violated: PhysicsModule must be registered "
    "before physics scenario initialization.");
  if (module == nullptr) {
    LOG_F(ERROR,
      "Physics: cannot initialize scenario because PhysicsModule is not "
      "available");
    return false;
  }
  const auto world_id = module->GetWorldId();
  DCHECK_F(world_id != physics::kInvalidWorldId,
    "Physics demo contract violated: PhysicsModule world id must be valid "
    "before physics scenario initialization.");
  if (world_id == physics::kInvalidWorldId) {
    LOG_F(ERROR,
      "Physics: cannot initialize scenario because PhysicsModule world id "
      "is invalid");
    return false;
  }

  for (auto& node : static_nodes_) {
    if (!node.IsAlive()) {
      continue;
    }

    const auto scale
      = node.GetTransform().GetLocalScale().value_or(Vec3 { 1.0F, 1.0F, 1.0F });
    const bool is_floor = node.GetName() == "Floor";
    const bool is_sphere
      = node.GetName().find("Bowl") != std::string_view::npos;
    const bool is_ramp_surface = node.GetName() == "Ramp";

    physics::body::BodyDesc desc {};
    desc.type = physics::body::BodyType::kStatic;
    desc.flags = physics::body::BodyFlags::kNone;
    if (is_floor) {
      desc.shape
        = physics::BoxShape { .extents = 0.5F * kFloorCollisionSize };
    } else if (is_sphere) {
      desc.shape = physics::SphereShape { .radius = 0.5F * scale.x };
    } else {
      desc.shape = physics::BoxShape { .extents = 0.5F * scale };
    }
    desc.initial_position = is_floor ? kFloorCollisionCenterWs
                                     : node.GetTransform().GetLocalPosition()
                                         .value_or(Vec3 { 0.0F, 0.0F, 0.0F });
    desc.initial_rotation = is_floor ? Quat { 1.0F, 0.0F, 0.0F, 0.0F }
                                     : node.GetTransform().GetLocalRotation()
                                         .value_or(Quat {
                                           1.0F, 0.0F, 0.0F, 0.0F });
    desc.friction = is_ramp_surface ? 0.14F : (is_sphere ? 0.80F : 0.88F);
    desc.restitution = is_sphere ? 0.03F : 0.02F;

    if (!AttachRigidBody(node, desc)) {
      LOG_F(ERROR, "Physics: failed to attach static body for node '{}'",
        node.GetName());
      return false;
    }
  }

  {
    physics::body::BodyDesc player_desc {};
    player_desc.type = physics::body::BodyType::kDynamic;
    player_desc.flags = physics::body::BodyFlags::kEnableGravity
      | physics::body::BodyFlags::kEnableContinuousCollisionDetection;
    player_desc.shape = physics::SphereShape { .radius = 0.5F };
    player_desc.initial_position = player_spawn_position_;
    player_desc.initial_rotation = player_spawn_rotation_;
    player_desc.mass_kg = 1.8F;
    player_desc.linear_damping = 0.38F;
    player_desc.angular_damping = 0.42F;
    player_desc.friction = 0.86F;
    player_desc.restitution = 0.02F;

    player_body_ = AttachRigidBody(player_node_, player_desc);
    if (!player_body_) {
      LOG_F(ERROR, "Physics: failed to attach player sphere body");
      return false;
    }
  }

  for (auto& obstacle : dynamic_obstacles_) {
    physics::body::BodyDesc desc {};
    desc.type = physics::body::BodyType::kDynamic;
    desc.flags = physics::body::BodyFlags::kEnableGravity;
    desc.shape = physics::BoxShape { .extents = Vec3 { 0.55F, 0.55F, 0.55F } };
    desc.initial_position = obstacle.spawn_position;
    desc.initial_rotation = obstacle.spawn_rotation;
    desc.mass_kg = 3.0F;
    desc.linear_damping = 0.18F;
    desc.angular_damping = 0.24F;
    desc.friction = 0.72F;
    desc.restitution = 0.03F;

    auto attached = AttachRigidBody(obstacle.node, desc);
    if (!attached) {
      LOG_F(ERROR, "Physics: failed to attach dynamic obstacle body");
      return false;
    }
    obstacle.body_id = *attached;
  }

  for (auto& flipper : flippers_) {
    physics::body::BodyDesc desc {};
    desc.type = physics::body::BodyType::kKinematic;
    desc.flags = physics::body::BodyFlags::kNone;
    desc.shape = physics::BoxShape { .extents = Vec3 { 0.9F, 0.175F, 0.175F } };
    desc.initial_position = flipper.position;
    desc.initial_rotation = MakeXRotationQuat(kRampPitchRad)
      * MakeZRotationQuat(flipper.rest_angle_rad);
    desc.mass_kg = 2.0F;
    desc.friction = 0.90F;
    desc.restitution = 0.0F;

    auto attached = AttachRigidBody(flipper.node, desc);
    if (!attached) {
      LOG_F(ERROR, "Physics: failed to attach flipper body");
      return false;
    }
    flipper.body_id = *attached;
  }

  physics_ready_ = true;
  pending_reset_ = false;
  return true;
}

auto MainModule::StageScenarioScene() -> bool
{
  constexpr size_t kDefaultSceneCapacity = 512;

  auto& shell = GetShell();
  shell.StageScene(
    std::make_unique<scene::Scene>("Physics-Scene", kDefaultSceneCapacity));
  const auto staged_scene = shell.GetStagedScene();
  CHECK_NOTNULL_F(staged_scene, "Physics staged scene is null");

  auto camera_node = staged_scene->CreateNode("MainCamera");
  auto camera = std::make_unique<scene::PerspectiveCamera>();
  CHECK_F(camera_node.AttachCamera(std::move(camera)),
    "Failed to attach PerspectiveCamera to MainCamera");
  auto tf = camera_node.GetTransform();
  tf.SetLocalPosition(Vec3 { 0.0F, -26.0F, 13.0F });
  tf.SetLocalRotation(glm::quat(glm::radians(Vec3 { -24.0F, 0.0F, 0.0F })));
  shell.SetStagedMainCamera(std::move(camera_node));
  sun_light_node_ = EnsureDefaultSceneLighting(*staged_scene,
    DefaultSceneLightingDesc {
      .sun_node_name = "SunLight",
      .sun_position = kSunPosition,
      .focus_point = kSceneLightingFocusPoint,
    });

  active_scene_ = {};
  main_camera_ = {};
  player_node_ = {};
  player_body_.reset();
  static_nodes_.clear();
  dynamic_obstacles_.clear();
  flippers_.clear();
  scene_ready_ = false;
  physics_ready_ = false;
  previous_player_world_position_.reset();
  player_speed_ = 0.0F;
  settle_timer_sec_ = 0.0F;
  settle_display_sec_ = 0.0F;
  player_settled_ = false;
  pending_launch_ = false;

  return true;
}

void MainModule::ResetScenario()
{
  if (!ResetGameplayState()) {
    LOG_F(
      WARNING, "Physics: gameplay reset skipped because scenario is not ready");
    return;
  }
  ++resets_count_;
}

auto MainModule::ResetGameplayState() -> bool
{
  if (!scene_ready_ || !physics_ready_ || !player_body_) {
    return false;
  }

  auto* module = ResolvePhysicsModule().get();
  if (module == nullptr) {
    return false;
  }
  const auto world_id = module->GetWorldId();
  if (world_id == physics::kInvalidWorldId) {
    return false;
  }

  bool reset_ok = true;
  auto reset_body_state = [&](const physics::BodyId body_id,
                            const Vec3& position, const Quat& rotation) {
    reset_ok &= module->Bodies()
                  .SetBodyPose(world_id, body_id, position, rotation)
                  .has_value();
    reset_ok
      &= module->Bodies()
           .SetLinearVelocity(world_id, body_id, Vec3 { 0.0F, 0.0F, 0.0F })
           .has_value();
    reset_ok
      &= module->Bodies()
           .SetAngularVelocity(world_id, body_id, Vec3 { 0.0F, 0.0F, 0.0F })
           .has_value();
  };

  {
    auto tf = player_node_.GetTransform();
    tf.SetLocalPosition(player_spawn_position_);
    tf.SetLocalRotation(player_spawn_rotation_);
    reset_body_state(
      *player_body_, player_spawn_position_, player_spawn_rotation_);
  }

  for (auto& obstacle : dynamic_obstacles_) {
    auto tf = obstacle.node.GetTransform();
    tf.SetLocalPosition(obstacle.spawn_position);
    tf.SetLocalRotation(obstacle.spawn_rotation);
    if (obstacle.body_id == physics::kInvalidBodyId) {
      reset_ok = false;
      continue;
    }
    reset_body_state(
      obstacle.body_id, obstacle.spawn_position, obstacle.spawn_rotation);
  }

  for (auto& flipper : flippers_) {
    const Quat rest_rotation = MakeXRotationQuat(kRampPitchRad)
      * MakeZRotationQuat(flipper.rest_angle_rad);
    flipper.elapsed_sec = flipper.swing_duration_sec;
    auto tf = flipper.node.GetTransform();
    tf.SetLocalPosition(flipper.position);
    tf.SetLocalRotation(rest_rotation);
    if (flipper.body_id == physics::kInvalidBodyId) {
      reset_ok = false;
      continue;
    }
    reset_body_state(flipper.body_id, flipper.position, rest_rotation);
  }

  previous_player_world_position_.reset();
  player_speed_ = 0.0F;
  settle_timer_sec_ = 0.0F;
  settle_display_sec_ = 0.0F;
  player_settled_ = false;
  pending_launch_ = false;
  pending_reset_ = false;

  return reset_ok;
}

void MainModule::LaunchSphere()
{
  if (!player_body_) {
    return;
  }
  auto* module = ResolvePhysicsModule().get();
  if (module == nullptr) {
    return;
  }
  const auto world_id = module->GetWorldId();
  const Vec3 launch_direction
    = glm::normalize(MakeXRotationQuat(kRampPitchRad) * space::move::Back);
  const auto set_velocity_result = module->Bodies().SetLinearVelocity(
    world_id, *player_body_, launch_direction * launch_impulse_);
  if (!set_velocity_result.has_value()) {
    LOG_F(ERROR, "Physics: launch failed to set player velocity");
    return;
  }

  ++launches_count_;
}

void MainModule::UpdateFlippers(const float dt_seconds)
{
  if (!physics_ready_) {
    return;
  }

  std::optional<Vec3> player_pos {};
  if (player_body_) {
    auto* module = ResolvePhysicsModule().get();
    if (module != nullptr) {
      const auto world_id = module->GetWorldId();
      const auto player_pos_result
        = module->Bodies().GetBodyPosition(world_id, *player_body_);
      if (player_pos_result.has_value()) {
        player_pos = player_pos_result.value();
      }
    }
  }
  for (auto& flipper : flippers_) {
    if (player_pos && flipper.elapsed_sec >= flipper.swing_duration_sec) {
      const float d = glm::distance(*player_pos, flipper.position);
      if (d < 1.4F && player_speed_ > 1.5F) {
        flipper.elapsed_sec = 0.0F;
        ++flipper_trigger_count_;
        ++contact_events_count_;
      }
    }

    flipper.elapsed_sec += dt_seconds;

    float angle = flipper.rest_angle_rad;
    if (flipper.elapsed_sec < flipper.swing_duration_sec) {
      const float t = flipper.elapsed_sec / flipper.swing_duration_sec;
      const float swing = std::sin(t * std::numbers::pi_v<float>);
      angle += flipper.direction_sign * flipper.max_swing_rad * swing;
    }

    auto tf = flipper.node.GetTransform();
    tf.SetLocalPosition(flipper.position);
    tf.SetLocalRotation(
      MakeXRotationQuat(kRampPitchRad) * MakeZRotationQuat(angle));
  }
}

auto MainModule::UpdateComposition(engine::FrameContext& context,
  std::vector<vortex::CompositionView>& views) -> void
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

  auto main_comp
    = vortex::CompositionView::ForScene(main_view_id_, view, main_camera_);
  main_comp.with_atmosphere = true;
  shell.OnMainViewReady(context, main_comp);
  views.push_back(std::move(main_comp));

  const auto imgui_view_id = GetOrCreateViewId("ImGuiView");
  views.push_back(vortex::CompositionView::ForImGui(
    imgui_view_id, view, [](graphics::CommandRecorder&) { }));
}

auto MainModule::UpdatePhysicsDemoPanelConfig(
  observer_ptr<ui::CameraRigController> camera_rig) -> void
{
  PhysicsDemoPanelConfig config {};
  config.camera_rig = camera_rig;
  config.launch_action = launch_action_;
  config.reset_action = reset_action_;
  config.nudge_left_action = nudge_left_action_;
  config.nudge_right_action = nudge_right_action_;

  config.pending_launch = &pending_launch_;
  config.pending_reset = &pending_reset_;
  config.launch_impulse = &launch_impulse_;
  config.player_speed = &player_speed_;
  config.player_settled = &player_settled_;

  config.launches_count = &launches_count_;
  config.resets_count = &resets_count_;
  config.contact_events_count = &contact_events_count_;
  config.flipper_trigger_count = &flipper_trigger_count_;

  config.settle_progress = &settle_display_sec_;
  config.settle_target = kSettleTimeRequiredSec;

  if (physics_panel_) {
    physics_panel_->Initialize(config);
  }
}

} // namespace oxygen::examples::physics_demo
