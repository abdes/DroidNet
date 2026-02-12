//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <chrono>
#include <cstring>
#include <memory>
#include <source_location>
#include <utility>

#include <imgui.h>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Core/FrameContext.h>
#include <Oxygen/Core/Types/ViewPort.h>
#include <Oxygen/Data/AssetKey.h>
#include <Oxygen/Data/GeometryAsset.h>
#include <Oxygen/Data/MaterialAsset.h>
#include <Oxygen/Data/PakFormat.h>
#include <Oxygen/Data/ProceduralMeshes.h>
#include <Oxygen/Data/ShaderReference.h>
#include <Oxygen/Engine/AsyncEngine.h>
#include <Oxygen/ImGui/ImGuiModule.h>
#include <Oxygen/Input/Action.h>
#include <Oxygen/Input/ActionTriggers.h>
#include <Oxygen/Input/InputActionMapping.h>
#include <Oxygen/Input/InputMappingContext.h>
#include <Oxygen/Input/InputSystem.h>
#include <Oxygen/OxCo/Co.h>
#include <Oxygen/Platform/Input.h>
#include <Oxygen/Platform/Window.h>
#include <Oxygen/Renderer/Renderer.h>
#include <Oxygen/Scene/Camera/Perspective.h>
#include <Oxygen/Scene/Scene.h>

#include "DemoShell/Runtime/CompositionView.h"
#include "DemoShell/Runtime/DemoAppContext.h"
#include "DemoShell/Runtime/ForwardPipeline.h"
#include "InputSystem/MainModule.h"

namespace {

constexpr std::uint32_t kWindowWidth = 2400;
constexpr std::uint32_t kWindowHeight = 1400;

// Helper: make a solid-color material asset snapshot (opaque by default)
auto MakeSolidColorMaterial(const char* name, const glm::vec4& rgba,
  oxygen::data::MaterialDomain domain = oxygen::data::MaterialDomain::kOpaque)
  -> std::shared_ptr<const oxygen::data::MaterialAsset>
{
  // NOLINTBEGIN(*-magic-numbers)
  namespace d = oxygen::data;
  namespace pak = oxygen::data::pak;

  pak::MaterialAssetDesc desc {};
  desc.header.asset_type = static_cast<uint8_t>(
    oxygen::data::AssetType::kMaterial); // MaterialAsset (for tooling/debug)
  // Safe copy name
  constexpr std::size_t maxn = sizeof(desc.header.name) - 1;
  const std::size_t n = (std::min)(maxn, std::strlen(name));
  std::memcpy(desc.header.name, name, n);
  desc.header.name[n] = '\0';
  desc.header.version = 1;
  desc.header.streaming_priority = 255;
  desc.material_domain = static_cast<uint8_t>(domain);
  desc.flags = 0;
  desc.shader_stages = 0;
  desc.base_color[0] = rgba.r;
  desc.base_color[1] = rgba.g;
  desc.base_color[2] = rgba.b;
  desc.base_color[3] = rgba.a;
  desc.normal_scale = 1.0F;
  desc.metalness = d::Unorm16 { 0.0F };
  desc.roughness = d::Unorm16 { 0.6F };
  desc.ambient_occlusion = d::Unorm16 { 1.0F };
  // Leave texture indices invalid (no textures)
  const d::AssetKey asset_key { .guid = d::GenerateAssetGuid() };
  return std::make_shared<const d::MaterialAsset>(
    asset_key, desc, std::vector<d::ShaderReference> {});
  // NOLINTEND(*-magic-numbers)
}

} // namespace

namespace oxygen::examples::input_system {

MainModule::MainModule(const DemoAppContext& app)
  : Base(app)
{
  DCHECK_NOTNULL_F(app_.platform);
  DCHECK_F(!app_.gfx_weak.expired());
}

auto MainModule::BuildDefaultWindowProperties() const
  -> platform::window::Properties
{
  platform::window::Properties p("Oxygen Input System");
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

auto MainModule::ClearBackbufferReferences() -> void
{
  if (pipeline_) {
    pipeline_->ClearBackbufferReferences();
  }
}

auto MainModule::OnAttachedImpl(
  oxygen::observer_ptr<oxygen::AsyncEngine> engine) noexcept
  -> std::unique_ptr<DemoShell>
{
  DCHECK_NOTNULL_F(engine, "expecting a valid engine");

  if (!InitInputBindings()) {
    return nullptr;
  }

  // Create Pipeline
  pipeline_
    = std::make_unique<ForwardPipeline>(observer_ptr { app_.engine.get() });

  // Initialize Shell
  auto shell = std::make_unique<DemoShell>();
  const auto demo_root
    = std::filesystem::path(std::source_location::current().file_name())
        .parent_path();
  DemoShellConfig shell_config {
    .engine = observer_ptr { app_.engine.get() },
    .enable_camera_rig = true, // Use DemoShell's camera rig for camera controls
    .content_roots = {
      .content_root = demo_root.parent_path() / "Content",
      .cooked_root = demo_root / ".cooked",
    },
    .panel_config = {
      .content_loader = false,
      .camera_controls = true, // Enable the camera controls panel
      .environment = true,
      .lighting = false,
      .rendering = false,
      .post_process = true,
    },
    .get_active_pipeline = [this]() -> observer_ptr<RenderingPipeline> {
      return observer_ptr { pipeline_.get() };
    },
  };

  if (!shell->Initialize(shell_config)) {
    LOG_F(WARNING, "InputSystem: DemoShell initialization failed");
    return nullptr;
  }

  // Register the InputDebugPanel
  input_debug_panel_ = std::make_shared<InputDebugPanel>();
  UpdateInputDebugPanelConfig(shell->GetCameraRig());
  if (!shell->RegisterPanel(input_debug_panel_)) {
    LOG_F(WARNING, "InputSystem: failed to register Input Debug panel");
    return nullptr;
  }

  // Create Main View ID
  main_view_id_ = GetOrCreateViewId("MainView");

  LOG_F(INFO, "InputSystem: Module initialized");
  return shell;
}

void MainModule::OnShutdown() noexcept
{
  auto& shell = GetShell();

  // Clear scene from shell first to ensure controlled destruction
  shell.SetScene(nullptr);

  input_debug_panel_.reset();
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

  LOG_SCOPE_F(3, "MainModule::OnFrameStart");

  // Set or create the scene now that the base has handled window/lifecycle
  if (!active_scene_.IsValid()) {
    auto scene = std::make_unique<scene::Scene>("InputSystem-Scene");
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

  frame_context.SetScene(shell.TryGetScene());

  // Ensure drone is configured once the rig is available
  const auto rig = shell.GetCameraRig();
  if (rig != last_camera_rig_) {
    last_camera_rig_ = rig;
    // Note: configure drone if needed, depends on demo specifics
  }
}

auto MainModule::OnFrameEnd(observer_ptr<engine::FrameContext> /*context*/)
  -> void
{
  LOG_SCOPE_F(3, "MainModule::OnFrameEnd");
}

auto MainModule::OnGameplay(observer_ptr<engine::FrameContext> context)
  -> co::Co<>
{
  // Check input edges during gameplay. InputSystem finalized edges during
  // kInput earlier in the frame; they remain valid until next frame start.
  namespace c = std::chrono;
  const auto u_game_dt = context->GetGameDeltaTime().get(); // nanoseconds
  const float dt = c::duration<float>(u_game_dt).count(); // seconds

  // Apply any pending sphere reset requested by UI toggles
  if (pending_ground_reset_) {
    pending_ground_reset_ = false;
    if (sphere_node_.IsAlive()) {
      auto tf = sphere_node_.GetTransform();
      tf.SetLocalPosition(sphere_base_pos_);
    }
    sphere_vel_z_ = 0.0F;
    sphere_in_air_ = false;
  }

  // Movement: ground vs swimming (Z is up in Oxygen)
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
    // Ground mode: jump actions
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

  // Integrate simple vertical physics when sphere is in air (Z is up)
  if (!swimming_mode_ && sphere_in_air_ && sphere_node_.IsAlive()) {
    sphere_vel_z_ += gravity_ * dt;
    auto tf = sphere_node_.GetTransform();
    const auto opt_pos = tf.GetLocalPosition();
    auto pos = opt_pos.value_or(sphere_base_pos_);
    pos.z += sphere_vel_z_ * dt;
    if (pos.z <= sphere_base_pos_.z) {
      pos.z = sphere_base_pos_.z;
      sphere_vel_z_ = 0.0F;
      sphere_in_air_ = false;
    }
    tf.SetLocalPosition(pos);
  }

  auto& shell = GetShell();
  shell.Update(context->GetGameDeltaTime());
  co_return;
}

auto MainModule::OnPreRender(observer_ptr<engine::FrameContext> context)
  -> co::Co<>
{
  DCHECK_NOTNULL_F(app_window_);

  if (!app_window_->GetWindow()) {
    DLOG_F(1, "OnPreRender: no valid window - skipping");
    co_return;
  }

  // Set ImGui context before making ImGui calls
  auto imgui_module_ref
    = app_.engine ? app_.engine->GetModule<imgui::ImGuiModule>() : std::nullopt;
  if (imgui_module_ref) {
    auto& imgui_module = imgui_module_ref->get();
    if (auto* imgui_context = imgui_module.GetImGuiContext()) {
      ImGui::SetCurrentContext(imgui_context);
    }
  }

  // Delegate to pipeline
  co_await Base::OnPreRender(context);
}

auto MainModule::OnSceneMutation(observer_ptr<engine::FrameContext> context)
  -> co::Co<>
{
  DCHECK_NOTNULL_F(app_window_);
  auto& shell = GetShell();

  if (!app_window_->GetWindow()) {
    DLOG_F(1, "OnSceneMutation: no valid window - skipping");
    co_return;
  }

  if (!active_scene_.IsValid()) {
    co_return;
  }

  const auto scene_ptr = shell.TryGetScene();
  if (!scene_ptr) {
    co_return;
  }
  auto* scene = scene_ptr.get();

  // Note: View camera is now updated via UpdateComposition

  // Build sphere mesh if not present
  using oxygen::data::MaterialAsset;
  using oxygen::data::MeshBuilder;
  using oxygen::data::pak::GeometryAssetDesc;
  using oxygen::data::pak::MeshViewDesc;

  if (!std::ranges::any_of(scene->GetRootNodes(),
        [](auto& n) { return n.GetName() == "Sphere"; })) {
    auto sphere_data = oxygen::data::MakeSphereMeshAsset(24, 48);
    if (sphere_data) {
      const auto sphere_mat
        = MakeSolidColorMaterial("SphereMat", { 0.85F, 0.2F, 0.2F, 1.0F });
      auto mesh
        = MeshBuilder(0, "SphereLOD0")
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

      // Geometry asset descriptor using mesh bounds
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
        oxygen::data::AssetKey { .guid = oxygen::data::GenerateAssetGuid() },
        geo_desc,
        std::vector<std::shared_ptr<oxygen::data::Mesh>> { std::move(mesh) });

      // Create a node and attach the geometry
      sphere_node_ = scene->CreateNode("Sphere");
      sphere_node_.GetRenderable().SetGeometry(std::move(sphere_geo));
      sphere_node_.GetTransform().SetLocalPosition(sphere_base_pos_);
    }
  }

  // Delegate to base class / pipeline for view registration
  co_await Base::OnSceneMutation(context);
}

auto MainModule::OnGuiUpdate(observer_ptr<engine::FrameContext> context)
  -> co::Co<>
{
  DCHECK_NOTNULL_F(app_window_);
  auto& shell = GetShell();

  if (app_window_->IsShuttingDown()) {
    DLOG_F(1, "OnGuiUpdate: window is closed/closing - skipping");
    co_return;
  }

  // Update panel config once when camera rig becomes available
  static bool camera_rig_bound = false;
  if (!camera_rig_bound && shell.GetCameraRig()) {
    UpdateInputDebugPanelConfig(shell.GetCameraRig());
    camera_rig_bound = true;
  }

  shell.Draw(context);

  co_return;
}

auto MainModule::InitInputBindings() noexcept -> bool
{
  // NOLINTBEGIN(*-magic-numbers)
  using oxygen::input::Action;
  using oxygen::input::ActionTriggerDown;
  using oxygen::input::ActionTriggerPressed;
  using oxygen::input::ActionTriggerTap;
  using oxygen::input::ActionValueType;
  using oxygen::input::InputActionMapping;
  using oxygen::input::InputMappingContext;

  using platform::InputSlots;

  if (!app_.input_system) {
    LOG_F(ERROR, "InputSystem not available; skipping input bindings");
    return false;
  }

  // Create and register actions we keep for later queries
  shift_action_ = std::make_shared<Action>("shift", ActionValueType::kBool);
  app_.input_system->AddAction(shift_action_);
  modifier_keys_ctx_ = std::make_shared<InputMappingContext>("modifier keys");
  {
    const auto trigger = std::make_shared<oxygen::input::ActionTriggerDown>();
    trigger->MakeExplicit();
    const auto left_shift_mapping = std::make_shared<InputActionMapping>(
      shift_action_, InputSlots::LeftShift);
    left_shift_mapping->AddTrigger(trigger);
    modifier_keys_ctx_->AddMapping(left_shift_mapping);
  }
  app_.input_system->AddMappingContext(modifier_keys_ctx_, 1000);

  // Example actions setup
  jump_action_ = std::make_shared<Action>("jump", ActionValueType::kBool);
  app_.input_system->AddAction(jump_action_);

  jump_higher_action_
    = std::make_shared<Action>("jump higher", ActionValueType::kBool);
  jump_higher_action_->SetConsumesInput(true);
  app_.input_system->AddAction(jump_higher_action_);

  swim_up_action_ = std::make_shared<Action>("swim up", ActionValueType::kBool);
  app_.input_system->AddAction(swim_up_action_);

  // Setup mapping context when moving on the ground
  ground_movement_ctx_
    = std::make_shared<InputMappingContext>("ground movement");
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

    app_.input_system->AddMappingContext(ground_movement_ctx_, 0);
  }

  // Setup mapping context when swimming
  swimming_ctx_ = std::make_shared<InputMappingContext>("swimming");
  {
    auto trig_down = std::make_shared<ActionTriggerDown>();
    trig_down->MakeExplicit();
    trig_down->SetActuationThreshold(0.1F);
    auto mapping = std::make_shared<InputActionMapping>(
      swim_up_action_, InputSlots::Space);
    mapping->AddTrigger(trig_down);
    swimming_ctx_->AddMapping(mapping);
    app_.input_system->AddMappingContext(swimming_ctx_, 0);
  }

  // Now the player is moving on the ground
  app_.input_system->ActivateMappingContext(modifier_keys_ctx_);
  app_.input_system->ActivateMappingContext(ground_movement_ctx_);
  // Camera controls are handled by DemoShell's camera rig

  return true;
}

//! Update the Input Debug panel configuration from current demo state.
auto MainModule::UpdateInputDebugPanelConfig(
  observer_ptr<ui::CameraRigController> camera_rig) -> void
{
  InputDebugPanelConfig config;
  config.input_system = observer_ptr { app_.input_system.get() };
  config.camera_rig = camera_rig;
  config.shift_action = shift_action_;
  config.jump_action = jump_action_;
  config.jump_higher_action = jump_higher_action_;
  config.swim_up_action = swim_up_action_;
  config.ground_movement_ctx = ground_movement_ctx_;
  config.swimming_ctx = swimming_ctx_;
  config.swimming_mode = &swimming_mode_;
  config.pending_ground_reset = &pending_ground_reset_;

  if (input_debug_panel_) {
    input_debug_panel_->Initialize(config);
  }
  // NOLINTEND(*-magic-numbers)
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

  const auto imgui_view_id = GetOrCreateViewId("ImGuiView");
  views.push_back(CompositionView::ForImGui(
    imgui_view_id, view, [](graphics::CommandRecorder&) { }));
}

} // namespace oxygen::examples::input_system
