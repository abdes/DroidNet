//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <type_traits>

#define GLM_ENABLE_EXPERIMENTAL
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/quaternion.hpp>

// Include first to avoid conflicts between winsock2(asio) and windows.h
#include <Oxygen/Platform/Platform.h>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Data/GeometryAsset.h>
#include <Oxygen/Data/MaterialAsset.h>
#include <Oxygen/Data/ProceduralMeshes.h>
#include <Oxygen/Graphics/Common/Buffer.h>
#include <Oxygen/Graphics/Common/CommandQueue.h>
#include <Oxygen/Graphics/Common/CommandRecorder.h>
#include <Oxygen/Graphics/Common/DescriptorAllocator.h>
#include <Oxygen/Graphics/Common/Graphics.h>
#include <Oxygen/Graphics/Common/PipelineState.h>
#include <Oxygen/Graphics/Common/Queues.h>
#include <Oxygen/Graphics/Common/RenderController.h>
#include <Oxygen/Graphics/Common/ResourceRegistry.h>
#include <Oxygen/Graphics/Common/Shaders.h>
#include <Oxygen/Graphics/Common/Surface.h>
#include <Oxygen/Graphics/Direct3D12/Allocator/D3D12MemAlloc.h>
#include <Oxygen/Platform/Window.h>
#include <Oxygen/Renderer/DepthPrePass.h>
#include <Oxygen/Renderer/RenderContext.h>
#include <Oxygen/Renderer/Renderer.h>
#include <Oxygen/Renderer/ShaderPass.h>
#include <Oxygen/Renderer/Types/View.h>
#include <Oxygen/Scene/Camera/Perspective.h>
#include <Oxygen/Scene/Scene.h>

#include "./MainModule.h"

using oxygen::examples::MainModule;
using WindowProps = oxygen::platform::window::Properties;
using WindowEvent = oxygen::platform::window::Event;
using oxygen::data::Mesh;
using oxygen::data::Vertex;
using oxygen::engine::RenderItem;
using oxygen::graphics::Buffer;
using oxygen::graphics::Framebuffer;

namespace {
constexpr std::uint32_t kWindowWidth = 1600;
constexpr std::uint32_t kWindowHeight = 900;
} // namespace

namespace {
// Centralize example scene state across frames.
struct ExampleState {
  std::shared_ptr<oxygen::scene::Scene> scene;
  oxygen::scene::SceneNode node_a; // Cube A
  oxygen::scene::SceneNode node_b; // Cube B
  oxygen::scene::SceneNode main_camera; // "MainCamera"
};

static ExampleState g_state;

// Ensure the example scene and two cubes exist; store persistent handles.
auto EnsureExampleScene() -> void
{
  if (g_state.scene) {
    return;
  }

  g_state.scene = std::make_shared<oxygen::scene::Scene>("ExampleScene");
  const std::shared_ptr<oxygen::data::Mesh> cube_mesh
    = oxygen::data::GenerateMesh("Cube/TestMesh", {});

  // Node A at origin
  auto node_a = g_state.scene->CreateNode("CubeA");
  node_a.AttachMesh(cube_mesh);
  g_state.node_a = node_a;

  // Node B offset on +X
  auto node_b = g_state.scene->CreateNode("CubeB");
  node_b.AttachMesh(cube_mesh);
  node_b.GetTransform().SetLocalPosition(glm::vec3(2.5F, 0.0F, 0.0F));
  g_state.node_b = node_b;

  LOG_F(INFO, "Scene created with two cube nodes");
}

// Find or create the "MainCamera" node with a PerspectiveCamera; keep aspect in
// sync.
auto EnsureMainCamera(const int width, const int height) -> void
{
  using oxygen::scene::PerspectiveCamera;
  using oxygen::scene::camera::ProjectionConvention;

  if (!g_state.scene) {
    return;
  }

  if (!g_state.main_camera.IsAlive()) {
    g_state.main_camera = g_state.scene->CreateNode("MainCamera");
  }

  if (!g_state.main_camera.HasCamera()) {
    auto camera
      = std::make_unique<PerspectiveCamera>(ProjectionConvention::kD3D12);
    const bool attached = g_state.main_camera.AttachCamera(std::move(camera));
    CHECK_F(attached, "Failed to attach PerspectiveCamera to MainCamera");
  }

  // Configure camera params (aspect from current surface size)
  const auto cam_ref = g_state.main_camera.GetCameraAs<PerspectiveCamera>();
  if (cam_ref) {
    const float aspect = height > 0
      ? (static_cast<float>(width) / static_cast<float>(height))
      : 1.0F;
    auto& cam = cam_ref->get();
    cam.SetFieldOfView(glm::radians(45.0F));
    cam.SetAspectRatio(aspect);
    cam.SetNearPlane(0.1F);
    cam.SetFarPlane(600.0F);
    cam.SetViewport(glm::ivec4 { 0, 0, width, height });
  }
}

// Update the MainCamera transform to look from position toward target with up.
auto UpdateMainCameraPose(const glm::vec3& position, const glm::vec3& target,
  const glm::vec3& up) -> void
{
  if (!g_state.main_camera.IsAlive()) {
    return;
  }

  auto t = g_state.main_camera.GetTransform();
  t.SetLocalPosition(position);

  // Build a rotation that looks at target. Use RH variant to match glm::lookAt.
  const glm::vec3 dir = glm::normalize(target - position);
  const glm::quat rot = glm::quatLookAtRH(dir, up);
  t.SetLocalRotation(rot);
}

// Animate the main camera along a smooth orbit/dolly path around the scene
// center with subtle height and radius modulation for a cinematic feel.
auto AnimateMainCamera(const float time_seconds) -> void
{
  // Scene center between the two cubes (kept consistent with setup)
  const glm::vec3 center(1.25F, 0.0F, 0.0F);

  // Base parameters
  const float base_radius = 6.0F;
  const float base_height = 1.6F;

  // Modulations for a more cinematic motion
  const float radius
    = base_radius + 1.25F * std::sin(0.35F * time_seconds); // slow dolly
  const float height
    = base_height + 0.45F * std::sin(0.8F * time_seconds + 0.7F); // bob
  const float angular_speed = 0.35F; // radians/sec (slow orbit)
  const float angle = angular_speed * time_seconds;

  // Orbit around center; keep negative Z bias to face the scene as in setup
  const glm::vec3 offset(
    radius * std::cos(angle), height, -radius * std::sin(angle));
  const glm::vec3 position = center + offset;
  const glm::vec3 target = center;
  const glm::vec3 up(0.0F, 1.0F, 0.0F);

  UpdateMainCameraPose(position, target, up);
}

} // namespace

MainModule::MainModule(
  std::shared_ptr<Platform> platform, std::weak_ptr<Graphics> gfx_weak)
  : platform_(std::move(platform))
  , gfx_weak_(std::move(gfx_weak))
{
  DCHECK_NOTNULL_F(platform_);
  DCHECK_F(!gfx_weak_.expired());
}

MainModule::~MainModule()
{
  LOG_SCOPE_F(INFO, "Destroying MainModule");

  // Flush command queues used for the surface
  if (!gfx_weak_.expired()) {
    const auto gfx = gfx_weak_.lock();
    const graphics::SingleQueueStrategy queues;
    gfx->GetCommandQueue(queues.GraphicsQueueName())->Flush();
  }

  // Un-register the vertex buffer view if it's valid
  // (No need to release descriptor handle, ResourceRegistry manages it)
  if (render_controller_ && renderer_) {
    try {
      auto& registry = render_controller_->GetResourceRegistry();
      const auto items = renderer_->GetOpaqueItems();
      if (!items.empty() && items.front().mesh) {
        auto vertex_buffer = renderer_->GetVertexBuffer(*items.front().mesh);
        registry.UnRegisterViews(*vertex_buffer);
      }
    } catch (const std::exception& e) {
      LOG_F(
        ERROR, "Error while un-registering vertex buffer view: {}", e.what());
    }
  }

  context_.framebuffer.reset(); // Do not hold onto the framebuffer
  framebuffers_.clear();
  surface_->DetachRenderer();
  renderer_.reset();
  render_controller_.reset();
  surface_.reset();
  platform_.reset();
}

auto MainModule::Run() -> void
{
  DCHECK_NOTNULL_F(nursery_);
  SetupCommandQueues();
  SetupMainWindow();
  SetupSurface();
  SetupRenderer();
  SetupShaders();
  surface_->AttachRenderer(render_controller_);

  nursery_->Start([this]() -> co::Co<> {
    while (!window_weak_.expired() && !gfx_weak_.expired()) {
      const auto gfx = gfx_weak_.lock();
      co_await gfx->OnRenderStart();
      // Submit the render task to the renderer
      render_controller_->Submit(
        [this]() -> co::Co<> { co_await RenderScene(); });
    }
  });
}

auto MainModule::SetupCommandQueues() const -> void
{
  CHECK_F(!gfx_weak_.expired());

  const auto gfx = gfx_weak_.lock();
  gfx->CreateCommandQueues(graphics::SingleQueueStrategy());
}

auto MainModule::SetupSurface() -> void
{
  CHECK_F(!gfx_weak_.expired());
  CHECK_F(!window_weak_.expired());

  const auto gfx = gfx_weak_.lock();

  const graphics::SingleQueueStrategy queues;
  surface_ = gfx->CreateSurface(
    window_weak_, gfx->GetCommandQueue(queues.GraphicsQueueName()));
  surface_->SetName("Main Window Surface");
  LOG_F(INFO, "Surface ({}) created for main window ({})", surface_->GetName(),
    window_weak_.lock()->Id());
}

auto MainModule::SetupMainWindow() -> void
{
  // Set up the main window
  WindowProps props("Oxygen Graphics Example");
  props.extent = { .width = kWindowWidth, .height = kWindowHeight };
  props.flags = { .hidden = false,
    .always_on_top = false,
    .full_screen = false,
    .maximized = false,
    .minimized = false,
    .resizable = true,
    .borderless = false };
  window_weak_ = platform_->Windows().MakeWindow(props);
  if (const auto window = window_weak_.lock()) {
    LOG_F(INFO, "Main window {} is created", window->Id());
  }

  // Immediately accept the close request for the main window
  nursery_->Start([this]() -> co::Co<> {
    while (!window_weak_.expired()) {
      const auto window = window_weak_.lock();
      co_await window->CloseRequested();
      // Stop the nursery
      if (nursery_) {
        nursery_->Cancel();
      }
      window_weak_.lock()->VoteToClose();
    }
  });

  nursery_->Start([this]() -> co::Co<> {
    while (!window_weak_.expired()) {
      const auto window = window_weak_.lock();
      if (const auto [from, to] = co_await window->Events().UntilChanged();
        to == WindowEvent::kResized) {
        LOG_F(INFO, "Main window was resized");
        surface_->ShouldResize(true);
        framebuffers_.clear();
      } else {
        if (to == WindowEvent::kExposed) {
          LOG_F(INFO, "My window is exposed");
        }
      }
    }
  });

  // Add a termination signal handler
  nursery_->Start([this]() -> co::Co<> {
    co_await platform_->Async().OnTerminate();
    LOG_F(INFO, "terminating...");
    // Terminate the application by requesting the main window to close
    window_weak_.lock()->RequestClose();
  });
}

auto MainModule::SetupRenderer() -> void
{
  CHECK_F(!gfx_weak_.expired());

  const auto gfx = gfx_weak_.lock();
  render_controller_ = gfx->CreateRenderController(
    "Main Window Renderer", surface_, kFrameBufferCount - 1);
  CHECK_NOTNULL_F(
    render_controller_, "Failed to create renderer for main window");
  renderer_ = std::make_shared<engine::Renderer>(render_controller_);
}

auto MainModule::SetupFramebuffers() -> void
{
  CHECK_F(!gfx_weak_.expired());
  auto gfx = gfx_weak_.lock();

  for (auto i = 0U; i < kFrameBufferCount; ++i) {
    graphics::TextureDesc depth_desc;
    depth_desc.width = kWindowWidth;
    depth_desc.height = kWindowHeight;
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

    framebuffers_.push_back(gfx->CreateFramebuffer(desc, *render_controller_));
    CHECK_NOTNULL_F(
      framebuffers_[i], "Failed to create framebuffer for main window");
  }
}

auto MainModule::SetupShaders() const -> void
{
  CHECK_F(!gfx_weak_.expired());
  const auto gfx = gfx_weak_.lock();

  // Verify that the shaders can be loaded by the Graphics backend
  const auto vertex_shader = gfx->GetShader(graphics::MakeShaderIdentifier(
    ShaderType::kVertex, "FullScreenTriangle.hlsl"));

  const auto pixel_shader = gfx->GetShader(graphics::MakeShaderIdentifier(
    ShaderType::kPixel, "FullScreenTriangle.hlsl"));

  CHECK_NOTNULL_F(
    vertex_shader, "Failed to load FullScreenTriangle vertex shader");
  CHECK_NOTNULL_F(
    pixel_shader, "Failed to load FullScreenTriangle pixel shader");

  LOG_F(INFO, "Engine shaders loaded successfully");
}

// Phase 2: SRVs and indices are ensured in Renderer::EnsureMeshResources.

// === Data-driven RenderItem/scene system integration ===

auto MainModule::RenderScene() -> co::Co<>
{
  if (gfx_weak_.expired()) {
    co_return;
  }

  if (framebuffers_.empty()) {
    SetupFramebuffers();
  }

  DLOG_F(1, "Rendering scene in frame index {}",
    render_controller_->CurrentFrameIndex());

  // Ensure example scene content and camera exist
  EnsureExampleScene();
  EnsureMainCamera(
    static_cast<int>(surface_->Width()), static_cast<int>(surface_->Height()));

  auto gfx = gfx_weak_.lock();
  const auto recorder = render_controller_->AcquireCommandRecorder(
    graphics::SingleQueueStrategy().GraphicsQueueName(),
    "Main Window Command List");

  const auto fb = framebuffers_[render_controller_->CurrentFrameIndex()];
  fb->PrepareForRender(*recorder);
  recorder->BindFrameBuffer(*fb);

  context_.framebuffer = fb;

  // --- DepthPrePass integration ---
  static std::shared_ptr<engine::DepthPrePass> depth_pass;
  static std::shared_ptr<engine::DepthPrePassConfig> depth_pass_config;
  if (!depth_pass_config) {
    depth_pass_config = std::make_shared<engine::DepthPrePassConfig>();
    depth_pass_config->debug_name = "ShaderPass";
  }
  if (!depth_pass) {
    depth_pass = std::make_shared<engine::DepthPrePass>(depth_pass_config);
  }

  // --- ShaderPass integration ---
  static std::shared_ptr<engine::ShaderPass> shader_pass;
  static std::shared_ptr<engine::ShaderPassConfig> shader_pass_config;
  if (!shader_pass_config) {
    shader_pass_config = std::make_shared<engine::ShaderPassConfig>();
    shader_pass_config->clear_color
      = graphics::Color { 0.1F, 0.2F, 0.38F, 1.0F }; // Custom clear color
    shader_pass_config->debug_name = "ShaderPass";
  }
  if (!shader_pass) {
    shader_pass = std::make_shared<engine::ShaderPass>(shader_pass_config);
  }

  // Animate rotation angle
  static float rotation_angle = 0.0F; // radians
  rotation_angle += 0.01F; // radians per frame, adjust as needed

  // Apply per-frame rotation (absolute) so we avoid accumulation drift
  const auto rot_y
    = glm::angleAxis(rotation_angle, glm::vec3(0.0F, 1.0F, 0.0F));
  if (g_state.node_a.IsAlive()) {
    g_state.node_a.GetTransform().SetLocalRotation(rot_y);
  }
  if (g_state.node_b.IsAlive()) {
    // Give the second cube a slightly different rotation for variety
    const auto rot = glm::angleAxis(
      -rotation_angle * 1.2F, glm::normalize(glm::vec3(0.25F, 1.0F, 0.0F)));
    g_state.node_b.GetTransform().SetLocalRotation(rot);
  }

  // Animate camera using wall-clock elapsed time for smooth motion
  using clock = std::chrono::steady_clock;
  static const auto t0 = clock::now();
  const float t = std::chrono::duration<float>(clock::now() - t0).count();
  AnimateMainCamera(t);

  if (renderer_) {
    oxygen::engine::CameraView::Params cv {
      .camera_node = g_state.main_camera,
      // Let camera ActiveViewport drive; we already keep camera viewport in
      // sync
      .viewport = std::nullopt,
      .scissor = std::nullopt,
      .pixel_jitter = glm::vec2(0.0F, 0.0F),
      .reverse_z = false,
      .mirrored = false,
    };
    renderer_->BuildFrame(*g_state.scene, oxygen::engine::CameraView(cv));
  }

  // Assemble and run the render graph
  co_await renderer_->ExecuteRenderGraph(
    [&](const engine::RenderContext& context) -> co::Co<> {
      // Depth Pre-Pass
      co_await depth_pass->PrepareResources(context, *recorder);
      co_await depth_pass->Execute(context, *recorder);
      // Shader Pass
      co_await shader_pass->PrepareResources(context, *recorder);
      co_await shader_pass->Execute(context, *recorder);
    },
    context_);

  co_return;
}
