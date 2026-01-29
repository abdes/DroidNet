//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <chrono>
#include <cstring>

#include <imgui.h>

#include <Oxygen/Core/Types/ViewPort.h>
#include <Oxygen/Data/AssetKey.h>
#include <Oxygen/Data/GeometryAsset.h>
#include <Oxygen/Data/MaterialAsset.h>
#include <Oxygen/Data/PakFormat.h>
#include <Oxygen/Data/ProceduralMeshes.h>
#include <Oxygen/Data/ShaderReference.h>
#include <Oxygen/Graphics/Common/CommandRecorder.h>
#include <Oxygen/Graphics/Common/Surface.h>
#include <Oxygen/ImGui/ImGuiModule.h>
#include <Oxygen/Input/Action.h>
#include <Oxygen/Input/ActionTriggers.h>
#include <Oxygen/Input/InputActionMapping.h>
#include <Oxygen/Input/InputMappingContext.h>
#include <Oxygen/Input/InputSystem.h>
#include <Oxygen/OxCo/Co.h>
#include <Oxygen/Platform/Window.h>
#include <Oxygen/Platform/input.h>
#include <Oxygen/Renderer/Passes/ShaderPass.h>
#include <Oxygen/Renderer/SceneCameraViewResolver.h>
#include <Oxygen/Scene/Camera/Perspective.h>
#include <Oxygen/Scene/Scene.h>

#include "DemoShell/Runtime/DemoAppContext.h"
#include "DemoShell/Runtime/SingleViewModuleBase.h"
#include "InputSystem/MainModule.h"

namespace {

constexpr std::uint32_t kWindowWidth = 2400;
constexpr std::uint32_t kWindowHeight = 1400;

// Helper: make a solid-color material asset snapshot (opaque by default)
auto MakeSolidColorMaterial(const char* name, const glm::vec4& rgba,
  oxygen::data::MaterialDomain domain = oxygen::data::MaterialDomain::kOpaque)
  -> std::shared_ptr<const oxygen::data::MaterialAsset>
{
  using namespace oxygen::data;

  pak::MaterialAssetDesc desc {};
  desc.header.asset_type = 7; // MaterialAsset (for tooling/debug)
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
  desc.normal_scale = 1.0f;
  desc.metalness = Unorm16 { 0.0f };
  desc.roughness = Unorm16 { 0.6f };
  desc.ambient_occlusion = Unorm16 { 1.0f };
  // Leave texture indices invalid (no textures)
  const AssetKey asset_key { .guid = GenerateAssetGuid() };
  return std::make_shared<const MaterialAsset>(
    asset_key, desc, std::vector<ShaderReference> {});
}

auto CheckLimits(float& direction, float& new_distance) -> void
{
  if (new_distance >= 320.0F) {
    new_distance = 320.0F;
    direction = -1.0F;
  }
  if (new_distance <= 10.0F) {
    new_distance = 10.0F;
    direction = 1.0F;
  }
}
} // namespace

namespace oxygen::examples::input {

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

auto MainModule::OnAttached(
  oxygen::observer_ptr<oxygen::AsyncEngine> engine) noexcept -> bool
{
  // Initialize the input system using the engine pointer provided by the
  // new EngineModule lifecycle. If engine is null, fail initialization.
  if (!engine) {
    return false;
  }

  // Let the base class create the example window and lifecycle helpers
  // (keeps examples DRY). Base::OnAttached will early-out for headless apps.
  if (!Base::OnAttached(engine)) {
    return false;
  }

  if (!InitInputBindings()) {
    return false;
  }

  file_browser_service_ = std::make_unique<FileBrowserService>();

  shell_ = std::make_unique<DemoShell>();
  DemoShellConfig shell_config;
  shell_config.input_system = observer_ptr { app_.input_system.get() };
  shell_config.file_browser_service
    = observer_ptr { file_browser_service_.get() };
  shell_config.panel_config = DemoShellPanelConfig {
    .content_loader = false,
    .camera_controls = false,
    .environment = true,
    .lighting = false,
    .rendering = false,
  };
  shell_config.enable_camera_rig = false;

  if (!shell_->Initialize(shell_config)) {
    LOG_F(WARNING, "InputSystem: DemoShell initialization failed");
    return false;
  }

  input_debug_panel_ = std::make_shared<InputDebugPanel>();
  UpdateInputDebugPanelConfig();
  if (!shell_->RegisterPanel(input_debug_panel_)) {
    LOG_F(WARNING, "InputSystem: failed to register Input Debug panel");
    return false;
  }

  return true;
}

void MainModule::OnShutdown() noexcept
{
  if (shell_) {
    shell_->SetScene(std::unique_ptr<scene::Scene> {});
    shell_->SetSkyboxService(observer_ptr<SkyboxService> { nullptr });
  }
  active_scene_ = {};

  input_debug_panel_.reset();
  shell_.reset();
  file_browser_service_.reset();
}

auto MainModule::OnFrameStart(engine::FrameContext& context) -> void
{
  // Delegate common per-example frame start handling to the base. The
  // example-specific portion (scene creation & context.SetScene) is
  // implemented in OnExampleFrameStart.
  Base::OnFrameStart(context);
}

// (OnFrameStart logic moved to ExampleModuleBase::OnFrameStart)

auto MainModule::OnFrameEnd(engine::FrameContext& /*context*/) -> void
{
  LOG_SCOPE_F(3, "MainModule::OnFrameEnd");
}

auto MainModule::OnGameplay(engine::FrameContext& context) -> co::Co<>
{
  // Check input edges during gameplay. InputSystem finalized edges during
  // kInput earlier in the frame; they remain valid until next frame start.
  using namespace std::chrono;
  const auto u_game_dt = context.GetGameDeltaTime().get(); // nanoseconds
  const float dt = duration<float>(u_game_dt).count(); // seconds

  // Apply any pending sphere reset requested by UI toggles
  if (pending_ground_reset_) {
    pending_ground_reset_ = false;
    if (sphere_node_.IsAlive()) {
      auto tf = sphere_node_.GetTransform();
      tf.SetLocalPosition(sphere_base_pos_);
    }
    sphere_vel_y_ = 0.0f;
    sphere_in_air_ = false;
  }

  // Camera zoom via mouse wheel actions
  if (zoom_in_action_ && zoom_in_action_->WasTriggeredThisFrame()) {
    if (main_camera_.IsAlive()) {
      auto tf = main_camera_.GetTransform();
      const auto opt_pos = tf.GetLocalPosition();
      auto pos = opt_pos.value_or(glm::vec3(0.0F, 0.0F, 5.0F));
      // Move towards sphere along -Z axis
      pos.z = (std::max)(pos.z - zoom_step_, min_cam_distance_);
      tf.SetLocalPosition(pos);
    }
  }
  if (zoom_out_action_ && zoom_out_action_->WasTriggeredThisFrame()) {
    if (main_camera_.IsAlive()) {
      auto tf = main_camera_.GetTransform();
      const auto opt_pos = tf.GetLocalPosition();
      auto pos = opt_pos.value_or(glm::vec3(0.0F, 0.0F, 5.0F));
      // Move away from sphere along +Z axis
      pos.z = (std::min)(pos.z + zoom_step_, max_cam_distance_);
      tf.SetLocalPosition(pos);
    }
  }

  // Camera pan via the input system pan action (Axis2D per-frame).
  // Sum all Axis2D deltas from this frame's transitions to avoid losing
  // motion when the mapping clears values after micro-updates.
  if (pan_action_ && main_camera_.IsAlive()) {
    glm::vec2 pan_delta(0.0f);
    // Guard by the action's declared value type to avoid throwing exceptions
    if (pan_action_->GetValueType()
      == oxygen::input::ActionValueType::kAxis2D) {
      for (const auto& tr : pan_action_->GetFrameTransitions()) {
        const auto& v = tr.value_at_transition.GetAs<oxygen::Axis2D>();
        if (std::abs(v.x) > 0.0f || std::abs(v.y) > 0.0f) {
          pan_delta.x += v.x;
          pan_delta.y += v.y;
        }
      }
    }
    if (std::abs(pan_delta.x) > 0.0f || std::abs(pan_delta.y) > 0.0f) {
      auto tf = main_camera_.GetTransform();
      auto pos = tf.GetLocalPosition().value_or(glm::vec3(0.0F, 0.0F, 5.0F));
      // Screen-right is +x, screen-down is +y; pan in world X/Y
      pos.x -= pan_delta.x * pan_sensitivity_;
      pos.y
        += pan_delta.y * pan_sensitivity_ * -1.0f; // invert for natural drag
      tf.SetLocalPosition(pos);
    }
  }

  // Movement: ground vs swimming
  if (swimming_mode_) {
    // Swimming: hold Space (swim_up_action_) to move upwards
    if (swim_up_action_ && swim_up_action_->IsOngoing()) {
      if (sphere_node_.IsAlive()) {
        auto tf = sphere_node_.GetTransform();
        auto pos = tf.GetLocalPosition().value_or(sphere_base_pos_);
        pos.y += swim_up_speed_ * dt;
        tf.SetLocalPosition(pos);
      }
    }
    // No gravity in swimming mode
    sphere_in_air_ = false;
    sphere_vel_y_ = 0.0f;
  } else {
    // Ground mode: jump actions
    if (jump_higher_action_ && jump_higher_action_->WasTriggeredThisFrame()) {
      if (!sphere_in_air_) {
        sphere_in_air_ = true;
        sphere_vel_y_ = jump_higher_impulse_;
      }
    } else if (jump_action_ && jump_action_->WasTriggeredThisFrame()) {
      if (!sphere_in_air_) {
        sphere_in_air_ = true;
        sphere_vel_y_ = jump_impulse_;
      }
    }
  }

  // Integrate simple vertical physics when sphere is in air
  if (!swimming_mode_ && sphere_in_air_ && sphere_node_.IsAlive()) {
    sphere_vel_y_ += gravity_ * dt;
    auto tf = sphere_node_.GetTransform();
    const auto opt_pos = tf.GetLocalPosition();
    auto pos = opt_pos.value_or(sphere_base_pos_);
    pos.y += sphere_vel_y_ * dt;
    if (pos.y <= sphere_base_pos_.y) {
      pos.y = sphere_base_pos_.y;
      sphere_vel_y_ = 0.0f;
      sphere_in_air_ = false;
    }
    tf.SetLocalPosition(pos);
  }

  if (shell_) {
    shell_->Update(context.GetGameDeltaTime());
  }
  co_return;
}

auto MainModule::OnPreRender(engine::FrameContext& context) -> co::Co<>
{
  // Ensure framebuffers are created after a resize
  DCHECK_NOTNULL_F(app_window_);

  // Set ImGui context before making ImGui calls
  auto imgui_module_ref = app_.engine->GetModule<imgui::ImGuiModule>();
  if (imgui_module_ref) {
    auto& imgui_module = imgui_module_ref->get();
    if (auto* imgui_context = imgui_module.GetImGuiContext()) {
      ImGui::SetCurrentContext(imgui_context);
    }
  }

  // Ensure render passes are created/configured via the example RenderGraph
  if (auto rg = GetRenderGraph(); rg) {
    rg->SetupRenderPasses();

    // Configure pass-specific settings
    auto shader_pass_config = rg->GetShaderPassConfig();
    if (shader_pass_config) {
      shader_pass_config->clear_color
        = graphics::Color { 0.1F, 0.2F, 0.38F, 1.0F };
      shader_pass_config->debug_name = "ShaderPass";
    }
  }

  // Register view resolver / render graph with the renderer via the helper
  // implemented by SingleViewExample.
  RegisterViewForRendering(main_camera_);

  co_return;
}

auto MainModule::OnExampleFrameStart(engine::FrameContext& context) -> void
{
  LOG_SCOPE_F(3, "MainModule::OnExampleFrameStart");

  // Set or create the scene now that the base has handled window/lifecycle
  // concerns for this frame.
  if (!active_scene_.IsValid()) {
    auto scene = std::make_unique<scene::Scene>("InputSystem-Scene");
    if (shell_) {
      active_scene_ = shell_->SetScene(std::move(scene));
    }
  }

  const auto scene_ptr
    = shell_ ? shell_->TryGetScene() : observer_ptr<scene::Scene> { nullptr };
  context.SetScene(observer_ptr { scene_ptr.get() });
}

auto MainModule::OnSceneMutation(engine::FrameContext& context) -> co::Co<>
{
  DCHECK_NOTNULL_F(app_window_);
  DCHECK_F(active_scene_.IsValid());

  const auto scene_ptr
    = shell_ ? shell_->TryGetScene() : observer_ptr<scene::Scene> { nullptr };
  auto* scene = scene_ptr.get();
  DCHECK_NOTNULL_F(scene);

  // Use base helper which registers views and invokes a ready callback
  UpdateFrameContext(context, [this](int w, int h) {
    EnsureMainCamera(w, h);
    RegisterViewForRendering(main_camera_);
    if (shell_) {
      shell_->SetActiveCamera(main_camera_);
    }
  });
  if (!app_window_->GetWindow()) {
    co_return;
  }

  if (shell_) {
    shell_->Update(time::CanonicalDuration {});
  }

  // Build a single-LOD sphere mesh using ProceduralMeshes + MeshBuilder
  using oxygen::data::MaterialAsset;
  using oxygen::data::MeshBuilder;
  using oxygen::data::pak::GeometryAssetDesc;
  using oxygen::data::pak::MeshViewDesc;

  if (!std::ranges::any_of(scene->GetRootNodes(),
        [](auto& n) { return n.GetName() == "Sphere"; })) {
    auto sphere_data = oxygen::data::MakeSphereMeshAsset(24, 48);
    if (sphere_data) {
      const auto sphere_mat
        = MakeSolidColorMaterial("SphereMat", { 0.85f, 0.2f, 0.2f, 1.0f });
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
      // Place sphere in front of the camera along -Z
      sphere_node_.GetTransform().SetLocalPosition(sphere_base_pos_);
    }
  }

  co_return;
}

auto MainModule::OnCompositing(engine::FrameContext& context) -> co::Co<>
{
  MarkSurfacePresentable(context);
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

  co_return;
}

// Window creation is managed by ExampleModuleBase/AppWindow — no local
// SetupMainWindow is necessary for this module.

// SetupRenderPasses is now part of the shared Examples/Common RenderGraph
// component. MainModule should call render_graph_->SetupRenderPasses when
// needed instead of having a private copy.

auto MainModule::EnsureMainCamera(const int width, const int height) -> void
{
  using scene::PerspectiveCamera;
  const auto scene_ptr
    = shell_ ? shell_->TryGetScene() : observer_ptr<scene::Scene> { nullptr };
  auto* scene = scene_ptr.get();
  if (!scene) {
    return;
  }

  if (!main_camera_.IsAlive()) {
    main_camera_ = scene->CreateNode("MainCamera");
  }

  if (!main_camera_.HasCamera()) {
    auto camera = std::make_unique<PerspectiveCamera>();
    const bool attached = main_camera_.AttachCamera(std::move(camera));
    CHECK_F(attached, "Failed to attach PerspectiveCamera to MainCamera");
    // Set an initial camera position looking towards -Z so the origin is
    // visible
    main_camera_.GetTransform().SetLocalPosition(glm::vec3(0.0F, 0.0F, 5.0F));
  }

  // Configure camera params
  const auto cam_ref = main_camera_.GetCameraAs<PerspectiveCamera>();
  if (cam_ref) {
    const float aspect = height > 0
      ? (static_cast<float>(width) / static_cast<float>(height))
      : 1.0F;
    auto& cam = cam_ref->get();
    cam.SetFieldOfView(glm::radians(45.0F));
    cam.SetAspectRatio(aspect);
    cam.SetNearPlane(0.1F);
    cam.SetFarPlane(600.0F);
    cam.SetViewport(ViewPort { .top_left_x = 0.0f,
      .top_left_y = 0.0f,
      .width = static_cast<float>(width),
      .height = static_cast<float>(height),
      .min_depth = 0.0f,
      .max_depth = 1.0f });
  }
}

auto MainModule::InitInputBindings() noexcept -> bool
{
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

  // Zoom actions bound to mouse wheel up/down
  zoom_in_action_ = std::make_shared<Action>("zoom in", ActionValueType::kBool);
  zoom_out_action_
    = std::make_shared<Action>("zoom out", ActionValueType::kBool);
  app_.input_system->AddAction(zoom_in_action_);
  app_.input_system->AddAction(zoom_out_action_);

  // Camera pan action and helper mouse button action (for chain conditions)
  pan_action_
    = std::make_shared<Action>("camera pan", ActionValueType::kAxis2D);
  app_.input_system->AddAction(pan_action_);
  left_mouse_action_ = std::make_shared<Action>("lmb", ActionValueType::kBool);
  app_.input_system->AddAction(left_mouse_action_);

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

  // Camera controls context (group camera actions here)
  camera_controls_ctx_
    = std::make_shared<InputMappingContext>("camera controls");
  {
    // Zoom in: Mouse wheel up
    {
      const auto trigger = std::make_shared<ActionTriggerTap>();
      trigger->SetTapTimeThreshold(0.25F);
      trigger->MakeExplicit();
      const auto mapping = std::make_shared<InputActionMapping>(
        zoom_in_action_, InputSlots::MouseWheelUp);
      mapping->AddTrigger(trigger);
      camera_controls_ctx_->AddMapping(mapping);
    }
    // Zoom out: Mouse wheel down
    {
      const auto trigger = std::make_shared<ActionTriggerTap>();
      trigger->SetTapTimeThreshold(0.25F);
      trigger->MakeExplicit();
      const auto mapping = std::make_shared<InputActionMapping>(
        zoom_out_action_, InputSlots::MouseWheelDown);
      mapping->AddTrigger(trigger);
      camera_controls_ctx_->AddMapping(mapping);
    }
    // LMB helper mapping (pressed/released)
    {
      const auto trig_down = std::make_shared<ActionTriggerDown>();
      trig_down->MakeExplicit();
      trig_down->SetActuationThreshold(0.1F);
      const auto mapping = std::make_shared<InputActionMapping>(
        left_mouse_action_, InputSlots::LeftMouseButton);
      mapping->AddTrigger(trig_down);
      camera_controls_ctx_->AddMapping(mapping);
    }
    // Pan mapping: MouseXY, explicit Down to accept deltas, plus implicit
    // chains to require SHIFT and LMB
    {
      const auto trig_move = std::make_shared<ActionTriggerDown>();
      trig_move->MakeExplicit();
      trig_move->SetActuationThreshold(0.0F); // any delta

      // Implicit: require shift held
      const auto shift_chain
        = std::make_shared<oxygen::input::ActionTriggerChain>();
      shift_chain->SetLinkedAction(shift_action_);
      shift_chain->MakeImplicit();
      shift_chain->RequirePrerequisiteHeld(true);

      // Implicit: require left mouse held
      const auto lmb_chain
        = std::make_shared<oxygen::input::ActionTriggerChain>();
      lmb_chain->SetLinkedAction(left_mouse_action_);
      lmb_chain->MakeImplicit();
      lmb_chain->RequirePrerequisiteHeld(true);

      const auto mapping = std::make_shared<InputActionMapping>(
        pan_action_, InputSlots::MouseXY);
      mapping->AddTrigger(trig_move);
      mapping->AddTrigger(shift_chain);
      mapping->AddTrigger(lmb_chain);
      camera_controls_ctx_->AddMapping(mapping);
    }

    app_.input_system->AddMappingContext(camera_controls_ctx_, 10);
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
  app_.input_system->ActivateMappingContext(camera_controls_ctx_);

  return true;
}

//! Update the Input Debug panel configuration from current demo state.
auto MainModule::UpdateInputDebugPanelConfig() -> void
{
  InputDebugPanelConfig config;
  config.input_system = observer_ptr { app_.input_system.get() };
  config.main_camera = &main_camera_;
  config.shift_action = shift_action_;
  config.jump_action = jump_action_;
  config.jump_higher_action = jump_higher_action_;
  config.swim_up_action = swim_up_action_;
  config.zoom_in_action = zoom_in_action_;
  config.zoom_out_action = zoom_out_action_;
  config.left_mouse_action = left_mouse_action_;
  config.pan_action = pan_action_;
  config.ground_movement_ctx = ground_movement_ctx_;
  config.swimming_ctx = swimming_ctx_;
  config.swimming_mode = &swimming_mode_;
  config.pending_ground_reset = &pending_ground_reset_;
  config.pan_sensitivity = &pan_sensitivity_;
  config.zoom_step = &zoom_step_;

  if (input_debug_panel_) {
    input_debug_panel_->Initialize(config);
  }
}

} // namespace oxygen::examples::input
