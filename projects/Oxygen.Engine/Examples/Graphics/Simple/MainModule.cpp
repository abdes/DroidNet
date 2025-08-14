//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
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

// ===================== DEBUGGING HISTORY & CONTRACTS =====================
//
// D3D12 Bindless Rendering Triangle: Lessons Learned (NEVER AGAIN!)
//
// 1. Culling & Winding Order:
//    - D3D12's default: counter-clockwise (CCW) triangles are front-facing.
//    - If your triangle is defined in clockwise (CW) order, it will be culled
//      (invisible) with default culling.
//    - Solution: Use CCW order for vertices, or set the rasterizer state to
//      match your winding.
//
// 2. Descriptor Table Offset vs. Heap Index:
//    - The SRV index written to the constant buffer (used by the shader) MUST
//      match the offset within the descriptor table bound for this draw, NOT
//      the global heap index. Index 0 is the first position in the bound table;
//      that is, the first descriptor after the CBV.
//    - If you use the global heap index, the shader will access the wrong
//      resource or nothing at all.
//    - Solution: Always write the offset within the currently bound descriptor
//      table to the constant buffer.
//
// CONTRACTS (DO NOT BREAK!):
// - Triangle vertices must be defined in CCW order for D3D12 default culling,
//   or the rasterizer state must be set to match.
// - The SRV index in the constant buffer must be the offset within the
//   descriptor table bound at draw time.
// - Do not confuse global heap indices with descriptor table offsets!
//
// If you see a blank screen or missing geometry, check these invariants first!
// ===========================================================================

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

  // Build a scene once and reuse it; populate with two cube nodes
  static std::shared_ptr<oxygen::scene::Scene> scene;
  static oxygen::scene::SceneNode node_a_handle; // persists across frames
  static oxygen::scene::SceneNode node_b_handle; // persists across frames
  if (!scene) {
    scene = std::make_shared<oxygen::scene::Scene>("ExampleScene");
    const std::shared_ptr<data::Mesh> cube_mesh
      = data::GenerateMesh("Cube/TestMesh", {});

    // Node A at origin
    auto node_a = scene->CreateNode("CubeA");
    node_a.AttachMesh(cube_mesh);
    node_a_handle = node_a; // keep a handle for per-frame animation

    // Node B offset on +X
    auto node_b = scene->CreateNode("CubeB");
    node_b.AttachMesh(cube_mesh);
    node_b.GetTransform().SetLocalPosition(glm::vec3(2.5F, 0.0F, 0.0F));
    node_b_handle = node_b; // keep a handle for per-frame animation

    LOG_F(INFO, "Scene created with two cube nodes");
  }

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
  if (node_a_handle.IsAlive()) {
    node_a_handle.GetTransform().SetLocalRotation(rot_y);
  }
  if (node_b_handle.IsAlive()) {
    // Give the second cube a slightly different rotation for variety
    const auto rot = glm::angleAxis(
      -rotation_angle * 1.2F, glm::normalize(glm::vec3(0.25F, 1.0F, 0.0F)));
    node_b_handle.GetTransform().SetLocalRotation(rot);
  }

  // Look from an angle to see both cubes clearly with closer positioning
  glm::vec3 camera_position = glm::vec3(
    1.25F, 1.5F, -5.0F); // Much closer to the cubes, centered between them
  glm::vec3 target
    = glm::vec3(1.25F, 0.0F, 0.0F); // Look at center between cubes (0 and 2.5)
  auto up = glm::vec3(0, 1, 0);

  const float aspect = static_cast<float>(surface_->Width())
    / static_cast<float>(surface_->Height());
  if (renderer_) {
    // Build View from camera, then build frame via extraction
    oxygen::engine::View::Params vp;
    vp.view = glm::lookAt(camera_position, target, up);
    vp.proj = glm::perspective(glm::radians(45.0F), aspect, 0.1F, 600.0F);
    vp.has_camera_position = true;
    vp.camera_position = camera_position;
    const oxygen::engine::View view(vp);
    renderer_->BuildFrame(*scene, view);
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
