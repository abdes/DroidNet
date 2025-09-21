//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include "./MainModule.h"

#include <random>

#include <Oxygen/Core/Types/ViewPort.h>
#include <Oxygen/Data/GeometryAsset.h>
#include <Oxygen/Data/MaterialAsset.h>
#include <Oxygen/Data/PakFormat.h>
#include <Oxygen/Data/ProceduralMeshes.h>
#include <Oxygen/Data/ShaderReference.h>
#include <Oxygen/Graphics/Common/CommandQueue.h>
#include <Oxygen/Graphics/Common/CommandRecorder.h>
#include <Oxygen/Graphics/Common/Framebuffer.h>
#include <Oxygen/Graphics/Common/Graphics.h>
#include <Oxygen/Graphics/Common/Queues.h>
#include <Oxygen/Graphics/Common/Surface.h>
#include <Oxygen/ImGui/ImGuiModule.h>
#include <Oxygen/Input/Action.h>
#include <Oxygen/Input/ActionTriggers.h>
#include <Oxygen/Input/InputActionMapping.h>
#include <Oxygen/Input/InputMappingContext.h>
#include <Oxygen/Input/InputSystem.h>
#include <Oxygen/OxCo/Co.h>
#include <Oxygen/Platform/input.h>
#include <Oxygen/Renderer/Renderer.h>
#include <Oxygen/Scene/Camera/Perspective.h>
#include <Oxygen/Scene/Detail/RenderableComponent.h>
#include <Oxygen/Scene/Scene.h>
#include <imgui.h>

#include <cstring>
#include <variant>

namespace {

constexpr std::uint32_t kWindowWidth = 1600;
constexpr std::uint32_t kWindowHeight = 900;

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
  desc.metalness = 0.0f;
  desc.roughness = 0.6f;
  desc.ambient_occlusion = 1.0f;
  // Leave texture indices invalid (no textures)
  return std::make_shared<const MaterialAsset>(
    desc, std::vector<ShaderReference> {});
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

namespace oxygen::engine::examples {

MainModule::MainModule(const AsyncEngineApp& app)
  : app_(app)
{
  DCHECK_NOTNULL_F(app_.platform);
  DCHECK_F(!app_.gfx_weak.expired());
}

auto MainModule::OnAttached(
  oxygen::observer_ptr<oxygen::AsyncEngine> engine) noexcept -> bool
{
  // Initialize the input system using the engine pointer provided by the
  // new EngineModule lifecycle. If engine is null, fail initialization.
  if (!engine) {
    return false;
  }

  if (!InitInputBindings()) {
    return false;
  }
  if (!SetupMainWindow()) {
    return false;
  }
  if (!SetupSurface()) {
    return false;
  }
  if (!SetupFramebuffers()) {
    return false;
  }

  return true;
}

void MainModule::OnShutdown() noexcept { }

auto MainModule::OnFrameStart(engine::FrameContext& context) -> void
{
  // Set the scene.
  if (!scene_) {
    scene_ = std::make_shared<scene::Scene>("InputSystem-Scene");
  }
  context.SetScene(observer_ptr { scene_.get() });

  // Register surface with the frame context
  if (surface_) {
    context.AddSurface(surface_);
  }

  // Configure ImGui to use our main window once after initialization
  if (auto imgui_module_ref = app_.engine->GetModule<imgui::ImGuiModule>()) {
    auto& imgui_module = imgui_module_ref->get();
    if (!window_weak_.expired()) {
      imgui_module.SetWindowId(window_weak_.lock()->Id());
    } else {
      imgui_module.SetWindowId(platform::kInvalidWindowId);
    }
  }
}

auto MainModule::OnFrameEnd(engine::FrameContext& /*context*/) -> void
{
  LOG_SCOPE_F(3, "MainModule::OnFrameEnd");
}

auto MainModule::OnGameplay(engine::FrameContext& /*context*/) -> co::Co<>
{
  // Check input edges during gameplay. InputSystem finalized edges during
  // kInput earlier in the frame; they remain valid until next frame start.
  using namespace std::chrono;

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
    for (const auto& tr : pan_action_->GetFrameTransitions()) {
      // Attempt Axis2D; skip if not set
      try {
        const auto& v = tr.value_at_transition.GetAs<Axis2D>();
        if (std::abs(v.x) > 0.0f || std::abs(v.y) > 0.0f) {
          pan_delta.x += v.x;
          pan_delta.y += v.y;
        }
      } catch (const std::bad_variant_access&) {
        // Ignore non-Axis2D transitions
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

  // Sphere jump actions
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

  // Integrate simple vertical physics when sphere is in air
  const auto now = steady_clock::now();
  if (last_frame_time_.time_since_epoch().count() == 0) {
    last_frame_time_ = now;
  }
  const float dt = duration<float>(now - last_frame_time_).count();
  last_frame_time_ = now;

  if (sphere_in_air_ && sphere_node_.IsAlive()) {
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
  co_return;
}

auto MainModule::OnFrameGraph(engine::FrameContext& /*context*/) -> co::Co<>
{
  // Ensure render passes are created/configured
  SetupRenderPasses();

  // Ensure ImGui context is set (safe to do here like in Async example)
  if (auto imgui_module_ref = app_.engine->GetModule<imgui::ImGuiModule>()) {
    auto& imgui_module = imgui_module_ref->get();
    if (auto* imgui_context = imgui_module.GetImGuiContext()) {
      ImGui::SetCurrentContext(imgui_context);
    }
  }
  co_return;
}

auto MainModule::OnSceneMutation(engine::FrameContext& context) -> co::Co<>
{
  if (!surface_ || window_weak_.expired()) {
    LOG_F(3, "Window or Surface is no longer valid");
    co_return;
  }

  DCHECK_NOTNULL_F(scene_);

  // Build a single-LOD sphere mesh using ProceduralMeshes + MeshBuilder
  using oxygen::data::MaterialAsset;
  using oxygen::data::MeshBuilder;
  using oxygen::data::pak::GeometryAssetDesc;
  using oxygen::data::pak::MeshViewDesc;

  if (scene_->IsEmpty()) {
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

      auto sphere_geo = std::make_shared<oxygen::data::GeometryAsset>(geo_desc,
        std::vector<std::shared_ptr<oxygen::data::Mesh>> { std::move(mesh) });

      // Create a node and attach the geometry
      sphere_node_ = scene_->CreateNode("Sphere");
      sphere_node_.GetRenderable().SetGeometry(std::move(sphere_geo));
      // Place sphere in front of the camera along -Z
      sphere_node_.GetTransform().SetLocalPosition(sphere_base_pos_);
    }
  }

  // Ensure the main camera exists and has a valid viewport
  EnsureMainCamera(
    static_cast<int>(surface_->Width()), static_cast<int>(surface_->Height()));

  // FIXME: view management is temporary
  context.AddView(std::make_shared<renderer::CameraView>(
    renderer::CameraView::Params {
      .camera_node = main_camera_,
      .viewport = std::nullopt,
      .scissor = std::nullopt,
      .pixel_jitter = glm::vec2(0.0F, 0.0F),
      .reverse_z = false,
      .mirrored = false,
    },
    surface_));

  co_return;
}

auto MainModule::OnGuiUpdate(engine::FrameContext& context) -> co::Co<>
{
  if (window_weak_.expired()) {
    co_return;
  }
  // Set ImGui current context before making any ImGui calls
  if (auto imgui_module_ref = app_.engine->GetModule<imgui::ImGuiModule>()) {
    auto& imgui_module = imgui_module_ref->get();
    if (auto* imgui_context = imgui_module.GetImGuiContext()) {
      ImGui::SetCurrentContext(imgui_context);
    }
  }

  DrawDebugOverlay(context);

  co_return;
}

auto MainModule::OnCommandRecord(engine::FrameContext& context) -> co::Co<>
{
  LOG_SCOPE_F(3, "MainModule::OnCommandRecord");

  if (!surface_ || window_weak_.expired()) {
    LOG_F(3, "Window or Surface is no longer valid");
    co_return;
  }

  if (app_.gfx_weak.expired()) {
    LOG_F(3, "Graphics no longer valid");
    co_return;
  }
  auto gfx = app_.gfx_weak.lock();

  auto queue_key = gfx->QueueKeyFor(graphics::QueueRole::kGraphics);
  auto recorder
    = gfx->AcquireCommandRecorder(queue_key, "Main Window Command List");

  if (!recorder) {
    LOG_F(ERROR, "Failed to acquire command recorder");
    co_return;
  }

  // Always render to the framebuffer that wraps the swapchain's current
  // backbuffer. The swapchain's backbuffer index may not match the engine's
  // frame slot due to resize or present timing; querying the surface avoids
  // D3D12 validation errors (WRONGSWAPCHAINBUFFERREFERENCE).
  const auto backbuffer_index = surface_->GetCurrentBackBufferIndex();
  if (framebuffers_.empty() || backbuffer_index >= framebuffers_.size()) {
    // Surface is not ready or has been torn down.
    co_return;
  }
  const auto fb = framebuffers_.at(backbuffer_index);
  if (!fb) {
    co_return;
  }
  fb->PrepareForRender(*recorder);
  recorder->BindFrameBuffer(*fb);

  // Create render context for renderer
  render_context_.framebuffer = fb;

  // Execute render graph using the configured passes
  co_await app_.renderer->ExecuteRenderGraph(
    [&](const engine::RenderContext& context) -> co::Co<> {
      // Depth Pre-Pass execution
      if (depth_pass_) {
        co_await depth_pass_->PrepareResources(context, *recorder);
        co_await depth_pass_->Execute(context, *recorder);
      }
      // Shader Pass execution
      if (shader_pass_) {
        co_await shader_pass_->PrepareResources(context, *recorder);
        co_await shader_pass_->Execute(context, *recorder);
      }
      // Transparent Pass execution (reuses color/depth from framebuffer)
      if (transparent_pass_) {
        // Assign attachments each frame (framebuffer back buffer + depth)
        if (fb) {
          transparent_pass_config_->color_texture
            = fb->GetDescriptor().color_attachments[0].texture;
          if (fb->GetDescriptor().depth_attachment.IsValid()) {
            transparent_pass_config_->depth_texture
              = fb->GetDescriptor().depth_attachment.texture;
          }
        }
        co_await transparent_pass_->PrepareResources(context, *recorder);
        co_await transparent_pass_->Execute(context, *recorder);
      }
      // --- ImGuiPass configuration ---
      auto imgui_module_ref = app_.engine->GetModule<imgui::ImGuiModule>();
      if (imgui_module_ref) {
        auto& imgui_module = imgui_module_ref->get();
        auto imgui_pass = imgui_module.GetRenderPass();
        if (imgui_pass) {
          co_await imgui_pass->Render(*recorder);
        }
      }
    },
    render_context_, context);
}

auto MainModule::SetupMainWindow() -> bool
{
  using WindowProps = oxygen::platform::window::Properties;

  // Set up the main window
  WindowProps props("Oxygen Graphics Demo - AsyncEngine");
  props.extent = { .width = kWindowWidth, .height = kWindowHeight };
  props.flags = {
    .hidden = false,
    .always_on_top = false,
    .full_screen = app_.fullscreen,
    .maximized = false,
    .minimized = false,
    .resizable = true,
    .borderless = false,
  };
  window_weak_ = app_.platform->Windows().MakeWindow(props);
  if (const auto window = window_weak_.lock()) {
    LOG_F(INFO, "Main window {} is created", window->Id());
    return true;
  }
  return false;
}

auto MainModule::SetupSurface() -> bool
{
  CHECK_F(!app_.gfx_weak.expired());
  CHECK_F(!window_weak_.expired());

  const auto gfx = app_.gfx_weak.lock();

  auto queue = gfx->GetCommandQueue(graphics::QueueRole::kGraphics);
  if (!queue) {
    LOG_F(ERROR, "No graphics command queue available to create surface");
    return false;
  }
  surface_ = gfx->CreateSurface(window_weak_, queue);
  surface_->SetName("Main Window Surface (AsyncEngine)");
  LOG_F(INFO, "Surface ({}) created for main window ({})", surface_->GetName(),
    window_weak_.lock()->Id());
  return true;
}

auto MainModule::SetupRenderPasses() -> void
{
  LOG_SCOPE_F(3, "MainModule::SetupRenderPasses");

  // --- DepthPrePass configuration ---
  if (!depth_pass_config_) {
    depth_pass_config_ = std::make_shared<engine::DepthPrePassConfig>();
    depth_pass_config_->debug_name = "DepthPrePass";
  }
  if (!depth_pass_) {
    depth_pass_ = std::make_shared<engine::DepthPrePass>(depth_pass_config_);
  }

  // --- ShaderPass configuration ---
  if (!shader_pass_config_) {
    shader_pass_config_ = std::make_shared<engine::ShaderPassConfig>();
    shader_pass_config_->clear_color
      = graphics::Color { 0.1F, 0.2F, 0.38F, 1.0F }; // Custom clear color
    shader_pass_config_->debug_name = "ShaderPass";
  }
  if (!shader_pass_) {
    shader_pass_ = std::make_shared<engine::ShaderPass>(shader_pass_config_);
  }

  // --- TransparentPass configuration ---
  if (!transparent_pass_config_) {
    transparent_pass_config_
      = std::make_shared<engine::TransparentPass::Config>();
    transparent_pass_config_->debug_name = "TransparentPass";
  }
  // Color/depth textures are assigned each frame just before execution (in
  // ExecuteRenderCommands)
  if (!transparent_pass_) {
    transparent_pass_
      = std::make_shared<engine::TransparentPass>(transparent_pass_config_);
  }
}

auto MainModule::EnsureMainCamera(const int width, const int height) -> void
{
  using scene::PerspectiveCamera;
  using scene::camera::ProjectionConvention;

  if (!scene_) {
    return;
  }

  if (!main_camera_.IsAlive()) {
    main_camera_ = scene_->CreateNode("MainCamera");
  }

  if (!main_camera_.HasCamera()) {
    auto camera
      = std::make_unique<PerspectiveCamera>(ProjectionConvention::kD3D12);
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
  using input::Action;
  using input::ActionTriggerDown;
  using input::ActionTriggerPressed;
  using input::ActionTriggerTap;
  using input::ActionValueType;
  using input::InputActionMapping;
  using input::InputMappingContext;

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
    // Zoom bindings moved to camera_controls_ctx_
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
    auto trigger = std::make_shared<ActionTriggerPressed>();
    auto mapping = std::make_shared<InputActionMapping>(
      swim_up_action_, InputSlots::Space);
    mapping->AddTrigger(trigger);
    swimming_ctx_->AddMapping(mapping);
    app_.input_system->AddMappingContext(swimming_ctx_, 0);
  }

  // Now the player is moving on the ground
  app_.input_system->ActivateMappingContext(modifier_keys_ctx_);
  app_.input_system->ActivateMappingContext(ground_movement_ctx_);
  app_.input_system->ActivateMappingContext(camera_controls_ctx_);

  return true;
}

auto MainModule::SetupFramebuffers() -> bool
{
  CHECK_F(!app_.gfx_weak.expired());
  CHECK_F(surface_ != nullptr, "Surface must be created before framebuffers");
  auto gfx = app_.gfx_weak.lock();

  // Get actual surface dimensions (important for full-screen mode)
  const auto surface_width = surface_->Width();
  const auto surface_height = surface_->Height();

  framebuffers_.clear();
  for (auto i = 0U; i < frame::kFramesInFlight.get(); ++i) {
    graphics::TextureDesc depth_desc;
    depth_desc.width = surface_width;
    depth_desc.height = surface_height;
    depth_desc.format = Format::kDepth32;
    depth_desc.texture_type = TextureType::kTexture2D;
    depth_desc.is_shader_resource = true;
    depth_desc.is_render_target = true;
    depth_desc.use_clear_value = true;
    depth_desc.clear_value = { 1.0F, 0.0F, 0.0F, 0.0F };
    depth_desc.initial_state = graphics::ResourceStates::kDepthWrite;
    const auto depth_tex = gfx->CreateTexture(depth_desc);

    auto desc = graphics::FramebufferDesc {}
                  .AddColorAttachment(surface_->GetBackBuffer(i))
                  .SetDepthAttachment(depth_tex);

    framebuffers_.push_back(gfx->CreateFramebuffer(desc));
    CHECK_NOTNULL_F(
      framebuffers_[i], "Failed to create framebuffer for main window");
  }
  return true;
}

//=== Debug Overlay Implementation ===========================================//

auto MainModule::DrawDebugOverlay(engine::FrameContext& /*context*/) -> void
{
  // Simple HelloWorld debug window
  ImGui::SetNextWindowPos(ImVec2(20, 20), ImGuiCond_FirstUseEver);
  ImGui::SetNextWindowSize(ImVec2(360, 320), ImGuiCond_FirstUseEver);
  if (ImGui::Begin("Input Debug", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
    ImGui::TextUnformatted("HelloWorld!");
    ImGui::Separator();

    // Camera position
    if (main_camera_.IsAlive()) {
      const auto tf = main_camera_.GetTransform();
      const auto pos
        = tf.GetLocalPosition().value_or(glm::vec3(0.0F, 0.0F, 5.0F));
      ImGui::Text("Camera Pos: (%.2f, %.2f, %.2f)", pos.x, pos.y, pos.z);
    } else {
      ImGui::TextUnformatted("Camera: <not alive>");
    }

    // ImGui mouse capture state
    const auto& io = ImGui::GetIO();
    ImGui::Text(
      "ImGui WantCaptureMouse: %s", io.WantCaptureMouse ? "true" : "false");
    ImGui::Separator();

    // Helper lambda to print action state concisely
    const auto draw_action
      = [](const char* label, const std::shared_ptr<input::Action>& a) {
          if (!a) {
            ImGui::Text("%s: <null>", label);
            return;
          }
          ImGui::Text("%s: ongoing=%s trig=%s comp=%s canc=%s rel=%s valUpd=%s",
            label, a->IsOngoing() ? "1" : "0",
            a->WasTriggeredThisFrame() ? "1" : "0",
            a->WasCompletedThisFrame() ? "1" : "0",
            a->WasCanceledThisFrame() ? "1" : "0",
            a->WasReleasedThisFrame() ? "1" : "0",
            a->WasValueUpdatedThisFrame() ? "1" : "0");
        };

    // Modifier and button actions
    draw_action("Shift", shift_action_);
    draw_action("LMB", left_mouse_action_);

    // Pan action details
    draw_action("Pan", pan_action_);
    if (pan_action_) {
      // Axis2D value (safe): the stored variant may be uninitialized (bool)
      // until the first MouseXY event updates it; guard access.
      bool printed_axis = false;
      if (pan_action_->WasValueUpdatedThisFrame() || pan_action_->IsOngoing()) {
        try {
          const auto& axis = pan_action_->GetValue().GetAs<Axis2D>();
          ImGui::Text("Pan Axis: (x=%.2f, y=%.2f)", axis.x, axis.y);
          printed_axis = true;
        } catch (const std::bad_variant_access&) {
          // Fallthrough to print placeholder
        }
      }
      if (!printed_axis) {
        ImGui::TextUnformatted("Pan Axis: <not set>");
      }

      // Also show per-frame summed delta from transitions
      glm::vec2 pan_delta(0.0f);
      for (const auto& tr : pan_action_->GetFrameTransitions()) {
        try {
          const auto& v = tr.value_at_transition.GetAs<Axis2D>();
          pan_delta.x += v.x;
          pan_delta.y += v.y;
        } catch (const std::bad_variant_access&) {
          // ignore
        }
      }
      if (std::abs(pan_delta.x) > 0.0f || std::abs(pan_delta.y) > 0.0f) {
        ImGui::Text(
          "Pan Delta (sum): (x=%.2f, y=%.2f)", pan_delta.x, pan_delta.y);
      }
    }

    // Zoom action edges
    draw_action("Zoom In", zoom_in_action_);
    draw_action("Zoom Out", zoom_out_action_);

    // Jump actions
    draw_action("Jump", jump_action_);
    draw_action("Jump Higher", jump_higher_action_);

    // Params
    ImGui::Separator();
    ImGui::Text(
      "pan_sensitivity=%.4f, zoom_step=%.3f", pan_sensitivity_, zoom_step_);
  }
  ImGui::End();
}

} // namespace oxygen::engine::examples
