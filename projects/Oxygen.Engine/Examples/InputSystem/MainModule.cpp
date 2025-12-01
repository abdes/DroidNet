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
#include <Oxygen/Platform/Window.h>
#include <Oxygen/Platform/input.h>
#include <Oxygen/Renderer/Renderer.h>
#include <Oxygen/Scene/Camera/Perspective.h>
#include <Oxygen/Scene/Detail/RenderableComponent.h>
#include <Oxygen/Scene/Scene.h>
#include <imgui.h>

#include "../Common/AsyncEngineApp.h"
#include "../Common/ExampleModuleBase.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <unordered_map>
#include <variant>

namespace {

constexpr std::uint32_t kWindowWidth = 1900;
constexpr std::uint32_t kWindowHeight = 900;

//=== ImGui Quick-Visual Helpers ---------------------------------------------//

//! Draw a simple keyboard/mouse keycap (rounded rect + centered label).
static void DrawKeycap(ImDrawList* dl, ImVec2 p, const char* label, ImU32 bg,
  ImU32 border, ImU32 text, float scale = 1.0f)
{
  const float pad = 6.0f * scale;
  const ImVec2 text_size = ImGui::CalcTextSize(label);
  const ImVec2 size
    = ImVec2(text_size.x + pad * 2.0f, text_size.y + pad * 1.5f);
  const float r = 6.0f * scale;

  const ImVec2 p_max(p.x + size.x, p.y + size.y);
  dl->AddRectFilled(p, p_max, bg, r);
  dl->AddRect(p, p_max, border, r, 0, 1.5f * scale);

  const ImVec2 tp = ImVec2(
    p.x + (size.x - text_size.x) * 0.5f, p.y + (size.y - text_size.y) * 0.5f);
  dl->AddText(tp, text, label, label + std::strlen(label));
}

//! Compute the rendered size of a keycap for spacing/layout.
static ImVec2 MeasureKeycap(const char* label, float scale = 1.0f)
{
  const float pad = 6.0f * scale;
  const ImVec2 text_size = ImGui::CalcTextSize(label);
  return ImVec2(text_size.x + pad * 2.0f, text_size.y + pad * 1.5f);
}

//! Draw a tiny horizontal analog bar for scalar values in [vmin, vmax].
static void DrawAnalogBar(ImDrawList* dl, ImVec2 p, ImVec2 sz, float v,
  float vmin = -1.0f, float vmax = 1.0f, ImU32 bg = IM_COL32(30, 30, 34, 255),
  ImU32 fg = IM_COL32(90, 170, 255, 255))
{
  const ImVec2 p_max(p.x + sz.x, p.y + sz.y);
  dl->AddRectFilled(p, p_max, bg, 3.0f);
  const float t = (v - vmin) / (vmax - vmin);
  const float tt = std::clamp(t, 0.0f, 1.0f);
  const ImVec2 fill = ImVec2(p.x + sz.x * tt, p.y + sz.y);
  dl->AddRectFilled(p, ImVec2(fill.x, fill.y), fg, 3.0f);
  // zero line
  const float zt = (-vmin) / (vmax - vmin);
  const float zx = p.x + sz.x * std::clamp(zt, 0.0f, 1.0f);
  dl->AddLine(ImVec2(zx, p.y), ImVec2(zx, p.y + sz.y),
    IM_COL32(180, 180, 190, 120), 1.0f);
}

//! Plot a tiny sparkline from a circular buffer (values[filled], head==next).
static void PlotSparkline(const char* id, const float* values, int filled,
  int head, int capacity, ImVec2 size)
{
  if (!values || filled <= 0) {
    return;
  }
  ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0, 0));
  ImGui::PushStyleColor(ImGuiCol_FrameBg, IM_COL32(0, 0, 0, 0));
  ImGui::PushStyleColor(ImGuiCol_PlotLines, IM_COL32(140, 200, 255, 220));
  // rotate to chronological order into a small local buffer
  static float tmp[128];
  const int N = (std::min)(filled, 128);
  const int cap = (std::min)(capacity, 128);
  // Start at the oldest sample index in the ring buffer
  const int start = (head - filled + cap) % cap;
  for (int i = 0; i < N; ++i) {
    tmp[i] = values[(start + i) % cap];
  }
  ImGui::PlotLines(id, tmp, N, 0, nullptr, -1.0f, 1.0f, size);
  ImGui::PopStyleColor(2);
  ImGui::PopStyleVar();
}

struct History {
  static constexpr int kCapacity = 64;
  float values[kCapacity] = {};
  int head = 0; // next write index
  int count = 0; // number of valid samples (<= kCapacity)
  void Push(float v) noexcept
  {
    values[head] = v;
    head = (head + 1) % kCapacity;
    if (count < kCapacity) {
      ++count;
    }
  }
};

//! Compute an action state label and color for a compact badge.
static std::pair<const char*, ImU32> ActionStateBadge(
  const std::shared_ptr<oxygen::input::Action>& a)
{
  if (!a) {
    return { "<null>", IM_COL32(80, 80, 90, 255) };
  }
  if (a->WasCanceledThisFrame()) {
    return { "Canceled", IM_COL32(230, 120, 70, 255) };
  }
  if (a->WasCompletedThisFrame()) {
    return { "Completed", IM_COL32(110, 200, 120, 255) };
  }
  if (a->WasTriggeredThisFrame()) {
    return { "Triggered", IM_COL32(90, 170, 255, 255) };
  }
  if (a->WasReleasedThisFrame()) {
    return { "Released", IM_COL32(160, 160, 200, 255) };
  }
  if (a->IsOngoing()) {
    return { "Ongoing", IM_COL32(200, 200, 80, 255) };
  }
  return { "Idle", IM_COL32(70, 70, 80, 255) };
}

//! Compute a scalar analog value in [-1, 1] for plotting bars/sparklines.
//! For bool actions, returns 0 or 1. For Axis2D, returns magnitude.
static float ActionScalarValue(
  const std::shared_ptr<oxygen::input::Action>& a) noexcept
{
  if (!a) {
    return 0.0f;
  }

  using oxygen::input::ActionValueType;
  const auto vt = a->GetValueType();
  switch (vt) {
  case ActionValueType::kAxis2D: {
    // Safe to access Axis2D
    const auto& axis = a->GetValue().GetAs<oxygen::Axis2D>();
    const float mag = std::sqrt(axis.x * axis.x + axis.y * axis.y);
    return (std::min)(mag, 1.0f);
  }
  case ActionValueType::kAxis1D: {
    const auto& ax = a->GetValue().GetAs<oxygen::Axis1D>();
    return std::clamp(ax.x, -1.0f, 1.0f);
  }
  case ActionValueType::kBool: {
    const bool b = a->GetValue().GetAs<bool>();
    return b ? 1.0f : 0.0f;
  }
  default:
    return 0.0f;
  }
}

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

namespace oxygen::examples::input {

MainModule::MainModule(const common::AsyncEngineApp& app)
  : Base(app)
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

  // Let the base class create the example window and lifecycle helpers
  // (keeps examples DRY). Base::OnAttached will early-out for headless apps.
  if (!Base::OnAttached(engine)) {
    return false;
  }

  if (!InitInputBindings()) {
    return false;
  }

  return true;
}

void MainModule::OnShutdown() noexcept { }

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
  co_return;
}

auto MainModule::OnFrameGraph(engine::FrameContext& /*context*/) -> co::Co<>
{
  // Ensure framebuffers are created after a resize (framebuffers are cleared
  // on OnFrameStart when Surface::ShouldResize() was set). Recreate them here
  // if necessary so the render graph has valid attachments.
  DCHECK_NOTNULL_F(app_window_);
  if (app_window_->GetFramebuffers().empty()) {
    app_window_->EnsureFramebuffers();
  }

  // Ensure render passes are created/configured via the example RenderGraph
  if (render_graph_) {
    render_graph_->SetupRenderPasses();
  }

  // Ensure ImGui context is set (safe to do here like in Async example)
  if (auto imgui_module_ref = app_.engine->GetModule<imgui::ImGuiModule>()) {
    auto& imgui_module = imgui_module_ref->get();
    if (auto* imgui_context = imgui_module.GetImGuiContext()) {
      ImGui::SetCurrentContext(imgui_context);
    }
  }
  co_return;
}

auto MainModule::OnExampleFrameStart(engine::FrameContext& context) -> void
{
  LOG_SCOPE_F(3, "MainModule::OnExampleFrameStart");

  // Set or create the scene now that the base has handled window/lifecycle
  // concerns for this frame.
  if (!scene_) {
    scene_ = std::make_shared<scene::Scene>("InputSystem-Scene");
  }
  context.SetScene(observer_ptr { scene_.get() });
}

auto MainModule::OnSceneMutation(engine::FrameContext& context) -> co::Co<>
{
  DCHECK_NOTNULL_F(app_window_);
  const auto wnd_weak = app_window_->GetWindowWeak();
  const auto surface = app_window_->GetSurface();
  if (!surface || wnd_weak.expired()) {
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
    static_cast<int>(surface->Width()), static_cast<int>(surface->Height()));

  // Create CameraView with the appropriate parameters
  camera_view_ = std::make_shared<renderer::CameraView>(
    renderer::CameraView::Params {
      .camera_node = main_camera_,
      .viewport = std::nullopt,
      .scissor = std::nullopt,
      .pixel_jitter = glm::vec2(0.0F, 0.0F),
      .reverse_z = false,
      .mirrored = false,
    },
    surface);

  // Add view to FrameContext with metadata
  view_id_ = context.AddView(engine::ViewContext { .name = "InputSystemView",
    .surface = *surface,
    .metadata = { .tag = "InputSystem_MainView" } });

  co_return;
}

auto MainModule::OnGuiUpdate(engine::FrameContext& context) -> co::Co<>
{
  DCHECK_NOTNULL_F(app_window_);
  const auto wnd_weak = app_window_->GetWindowWeak();
  if (wnd_weak.expired()) {
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
  using namespace oxygen::graphics;

  LOG_SCOPE_F(3, "MainModule::OnCommandRecord");

  DCHECK_NOTNULL_F(app_window_);
  const auto wnd_weak = app_window_->GetWindowWeak();
  const auto surface = app_window_->GetSurface();
  if (!surface || wnd_weak.expired()) {
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
  const auto backbuffer_index = surface->GetCurrentBackBufferIndex();
  DCHECK_NOTNULL_F(app_window_);
  const auto& framebuffers = app_window_->GetFramebuffers();
  if (framebuffers.empty() || backbuffer_index >= framebuffers.size()) {
    // Surface is not ready or has been torn down.
    co_return;
  }
  const auto fb = framebuffers.at(backbuffer_index);
  if (!fb) {
    co_return;
  }
  // Manual resource tracking replacing PrepareForRender
  const auto& fb_desc = fb->GetDescriptor();
  for (const auto& attachment : fb_desc.color_attachments) {
    if (attachment.texture) {
      recorder->BeginTrackingResourceState(
        *attachment.texture, ResourceStates::kPresent, true);
      recorder->RequireResourceState(
        *attachment.texture, ResourceStates::kRenderTarget);
    }
  }
  if (fb_desc.depth_attachment.texture) {
    recorder->BeginTrackingResourceState(
      *fb_desc.depth_attachment.texture, ResourceStates::kDepthWrite, true);

    // Flush barriers to ensure all resource state transitions are applied and
    // that subsequent state transitions triggered by the frame rendering task
    // (application) are executed in a separate batch.
    recorder->FlushBarriers();
  }
  recorder->BindFrameBuffer(*fb);

  // Create render context for renderer (delegated to RenderGraph component)
  DCHECK_NOTNULL_F(render_graph_);
  render_graph_->PrepareForRenderFrame(fb);

  // Resolve the camera view to get the View snapshot
  if (!camera_view_) {
    LOG_F(ERROR, "CameraView not available");
    co_return;
  }
  const auto view_snapshot = camera_view_->Resolve();

  // Drive the renderer: BuildFrame ensures scene is prepared for rendering
  app_.renderer->BuildFrame(view_snapshot, context);

  // Execute render graph using the configured passes
  co_await app_.renderer->ExecuteRenderGraph(
    [&](const engine::RenderContext& render_context) -> co::Co<> {
      // Run all passes via RenderGraph (DepthPrePass, ShaderPass,
      // TransparentPass)
      co_await render_graph_->RunPasses(render_context, *recorder);

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
    render_graph_->GetRenderContext(), context);

  // Update FrameContext with the output framebuffer
  context.SetViewOutput(view_id_, fb);

  // Mark the surface as presentable
  const auto surfaces = context.GetSurfaces();
  for (size_t i = 0; i < surfaces.size(); ++i) {
    if (surfaces[i] == surface) {
      context.SetSurfacePresentable(i, true);
      LOG_F(3, "Marked surface at index {} as presentable", i);
      break;
    }
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

//=== Debug Overlay Implementation ===========================================//

auto MainModule::DrawDebugOverlay(engine::FrameContext& /*context*/) -> void
{
  // Quick win UI: compact action cards with keycaps, bar, sparkline.
  ImGui::SetNextWindowPos(ImVec2(20, 20), ImGuiCond_FirstUseEver);
  ImGui::SetNextWindowSize(ImVec2(460, 420), ImGuiCond_FirstUseEver);
  if (!ImGui::Begin(
        "Input Debug", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
    ImGui::End();
    return;
  }

  // Camera position
  if (main_camera_.IsAlive()) {
    const auto tf = main_camera_.GetTransform();
    const auto pos
      = tf.GetLocalPosition().value_or(glm::vec3(0.0F, 0.0F, 5.0F));
    ImGui::Text("Camera: (%.2f, %.2f, %.2f)", pos.x, pos.y, pos.z);
  } else {
    ImGui::TextUnformatted("Camera: <not alive>");
  }

  // ImGui mouse capture state
  const auto& io = ImGui::GetIO();
  ImGui::Text("WantCaptureMouse: %s", io.WantCaptureMouse ? "true" : "false");
  ImGui::Separator();

  // Toggle between Ground and Swimming mapping contexts
  {
    bool prev_mode = swimming_mode_;
    ImGui::Checkbox("Swimming mode", &swimming_mode_);
    if (swimming_mode_ != prev_mode && app_.input_system) {
      if (swimming_mode_) {
        app_.input_system->DeactivateMappingContext(ground_movement_ctx_);
        app_.input_system->ActivateMappingContext(swimming_ctx_);
      } else {
        app_.input_system->DeactivateMappingContext(swimming_ctx_);
        app_.input_system->ActivateMappingContext(ground_movement_ctx_);
        // Defer sphere reset to avoid accessing world data before propagation
        pending_ground_reset_ = true;
      }
    }
  }

  // Gather actions to show. Keep labels small and stable.
  struct Row {
    const char* label;
    std::shared_ptr<oxygen::input::Action> act;
  };
  Row rows[] = {
    { "Shift", shift_action_ },
    { "LMB", left_mouse_action_ },
    { "Pan", pan_action_ },
    { "Zoom In", zoom_in_action_ },
    { "Zoom Out", zoom_out_action_ },
    { "Jump", jump_action_ },
    { "Jump Higher", jump_higher_action_ },
    { "Swim Up", swim_up_action_ },
  };

  // Keep defined order; no sorting.

  static bool show_inactive = true;
  ImGui::Checkbox("Show inactive", &show_inactive);
  ImGui::Spacing();

  // Per-action history ring buffers keyed by label pointer (stable literals).
  static std::unordered_map<const char*, History> histories;

  // Table layout for cards: Label | Glyphs | State | Value | Sparkline
  if (ImGui::BeginTable("##actions", 5, ImGuiTableFlags_SizingStretchProp)) {
    ImGui::TableSetupColumn("Action", ImGuiTableColumnFlags_WidthFixed, 110.0f);
    ImGui::TableSetupColumn(
      "Bindings", ImGuiTableColumnFlags_WidthFixed, 200.0f);
    ImGui::TableSetupColumn("State", ImGuiTableColumnFlags_WidthFixed, 90.0f);
    ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch, 1.0f);
    ImGui::TableSetupColumn(
      "History", ImGuiTableColumnFlags_WidthStretch, 1.0f);

    // Track recent triggers to flash a fading highlight per action row.
    static std::unordered_map<const char*, double> last_trigger_time;
    const double now = ImGui::GetTime();

    for (const auto& r : rows) {
      const bool active = r.act
        && (r.act->IsOngoing() || r.act->WasTriggeredThisFrame()
          || r.act->WasCompletedThisFrame() || r.act->WasReleasedThisFrame()
          || r.act->WasCanceledThisFrame()
          || r.act->WasValueUpdatedThisFrame());
      if (!show_inactive && !active) {
        continue;
      }

      // Bump trigger time stamp
      if (r.act && r.act->WasTriggeredThisFrame()) {
        last_trigger_time[r.label] = now;
      }

      // Scope all row widgets with a unique ID to avoid label collisions
      ImGui::PushID(r.label);

      // Update history with current scalar value
      const float v = ActionScalarValue(r.act);
      auto& hist = histories[r.label];
      hist.Push(v);

      const auto [state_text, state_col] = ActionStateBadge(r.act);

      ImGui::TableNextRow();
      // Flash background highlight for recent trigger
      {
        float flash_alpha = 0.0f;
        constexpr float kFlashDuration = 1.5f; // seconds
        auto it = last_trigger_time.find(r.label);
        if (it != last_trigger_time.end()) {
          const float age = static_cast<float>(now - it->second);
          if (age >= 0.0f && age < kFlashDuration) {
            float t = 1.0f - (age / kFlashDuration);
            t = t * t; // ease-out quad
            flash_alpha = t;
          }
        }
        if (flash_alpha > 0.0f) {
          const int a = static_cast<int>(flash_alpha * 110.0f);
          const ImU32 col = IM_COL32(255, 220, 120, a);
          ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg0, col);
        }
      }

      // Action label
      ImGui::TableSetColumnIndex(0);
      ImGui::AlignTextToFramePadding();
      ImGui::TextUnformatted(r.label);

      // Bindings (quick illustrative keycaps; not wired to real mappings yet)
      ImGui::TableSetColumnIndex(1);
      {
        ImDrawList* dl = ImGui::GetWindowDrawList();
        ImVec2 p = ImGui::GetCursorScreenPos();
        const float scale = ImGui::GetIO().FontGlobalScale;
        const float gap = 8.0f * scale;
        float used_w = 0.0f;
        float used_h = 0.0f;
        auto draw_and_advance = [&](const char* cap) {
          const ImVec2 sz = MeasureKeycap(cap, scale);
          DrawKeycap(dl, p, cap, IM_COL32(40, 40, 46, 255),
            IM_COL32(80, 80, 90, 255), IM_COL32(230, 230, 240, 255), scale);
          p.x += sz.x + gap;
          used_w += sz.x + gap;
          used_h = (std::max)(used_h, sz.y);
        };

        if (r.label == std::string("Pan")) {
          draw_and_advance("Shift");
          draw_and_advance("LMB");
        } else if (r.label == std::string("Zoom In")) {
          draw_and_advance("Wheel+");
        } else if (r.label == std::string("Zoom Out")) {
          draw_and_advance("Wheel-");
        } else if (r.label == std::string("Jump")) {
          draw_and_advance("Space");
        } else if (r.label == std::string("Jump Higher")) {
          draw_and_advance("Shift");
          draw_and_advance("Space");
        } else if (r.label == std::string("Shift")) {
          draw_and_advance("Shift");
        } else if (r.label == std::string("LMB")) {
          draw_and_advance("LMB");
        } else if (r.label == std::string("Swim Up")) {
          draw_and_advance("Space");
        }
        if (used_w > 0.0f && used_h > 0.0f) {
          // Remove trailing gap from width reservation
          used_w -= gap;
          ImGui::Dummy(ImVec2(used_w, used_h));
        } else {
          ImGui::Dummy(ImVec2(1, 26.0f * scale));
        }
      }

      // State badge
      ImGui::TableSetColumnIndex(2);
      ImGui::AlignTextToFramePadding();
      {
        ImGui::PushStyleColor(ImGuiCol_FrameBg, state_col);
        ImGui::TextDisabled("  %s  ", state_text);
        ImGui::PopStyleColor();
      }

      // Analog bar
      ImGui::TableSetColumnIndex(3);
      {
        ImDrawList* dl = ImGui::GetWindowDrawList();
        ImVec2 p = ImGui::GetCursorScreenPos();
        DrawAnalogBar(dl, p, ImVec2(160, 8), v, 0.0f, 1.0f);
        ImGui::Dummy(ImVec2(160, 10));
      }

      // Sparkline
      ImGui::TableSetColumnIndex(4);
      PlotSparkline("##spark", hist.values, hist.count, hist.head,
        History::kCapacity, ImVec2(160, 28));

      ImGui::PopID();
    }
    ImGui::EndTable();
  }

  ImGui::Separator();
  ImGui::Text(
    "pan_sensitivity=%.4f, zoom_step=%.3f", pan_sensitivity_, zoom_step_);
  ImGui::End();
}

} // namespace oxygen::examples::input
