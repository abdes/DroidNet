//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <type_traits>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Data/MeshAsset.h>
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
#include <Oxygen/Platform/Platform.h>
#include <Oxygen/Platform/Window.h>
#include <Oxygen/Renderer/RenderContext.h>
#include <Oxygen/Renderer/RenderItem.h>
#include <Oxygen/Renderer/Renderer.h>
#include <Oxygen/Renderer/ShaderPass.h>

#include <MainModule.h>

using oxygen::examples::MainModule;
using WindowProps = oxygen::platform::window::Properties;
using WindowEvent = oxygen::platform::window::Event;
using oxygen::data::MeshAsset;
using oxygen::data::Vertex;
using oxygen::engine::RenderItem;
using oxygen::graphics::Buffer;
using oxygen::graphics::Framebuffer;

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

namespace {

//! Creates a new MeshAsset representing a single triangle (for demo/testing
//! only).
auto MakeTriangleMeshAsset() -> std::shared_ptr<MeshAsset>
{
  // The shader expects only position (vec3) and color (vec3) tightly packed.
  // We must ensure the buffer layout matches exactly.
  std::vector<Vertex> vertices = {
    // CCW order for D3D12 default culling
    // clang-format off
    // position                normal     texcoord     tangent     bitangent    color (only .xyz used)
    { { -0.5f, -0.5f, 0.0f }, { 0, 0, 1 }, { 0, 0 }, { 1, 0, 0 }, { 0, 1, 0 }, { 1, 0, 0, 1 } }, // Red
    { {  0.0f,  0.5f, 0.0f }, { 0, 0, 1 }, { 0, 1 }, { 1, 0, 0 }, { 0, 1, 0 }, { 0, 1, 0, 1 } }, // Green
    { {  0.5f, -0.5f, 0.0f }, { 0, 0, 1 }, { 1, 0 }, { 1, 0, 0 }, { 0, 1, 0 }, { 0, 0, 1, 1 } }, // Blue
    // clang-format on
  };
  // NOTE: The shader will only read position and color (first 6 floats).
  // If the buffer is created from Vertex, ensure the stride matches the
  // shader's expectation. If not, create a tightly packed struct or buffer for
  // demo.
  std::vector<uint32_t> indices = { 0, 1, 2 };
  auto asset = std::make_shared<MeshAsset>(
    "Triangle", std::move(vertices), std::move(indices));
  asset->CreateView("default", 0, asset->VertexCount(), 0, asset->IndexCount());
  return asset;
}

//! Creates a new MeshAsset representing a single quad (for demo/testing only).
auto MakeQuadMeshAsset() -> std::shared_ptr<MeshAsset>
{
  // CCW order for D3D12 default culling
  constexpr float kHalfSize = 0.5f; // 50% of viewport (from -0.25 to +0.25)
  std::vector<Vertex> vertices = {
    // clang-format off
    // position                  normal     texcoord   tangent     bitangent    color
    { { -kHalfSize, -kHalfSize, 0.0f }, { 0, 0, 1 }, { 0, 0 }, { 1, 0, 0 }, { 0, 1, 0 }, { 1, 1, 0, 1 } }, // Bottom-left
    { {  kHalfSize, -kHalfSize, 0.0f }, { 0, 0, 1 }, { 1, 0 }, { 1, 0, 0 }, { 0, 1, 0 }, { 0, 1, 1, 1 } }, // Bottom-right
    { {  kHalfSize,  kHalfSize, 0.0f }, { 0, 0, 1 }, { 1, 1 }, { 1, 0, 0 }, { 0, 1, 0 }, { 1, 0, 1, 1 } }, // Top-right
    { { -kHalfSize,  kHalfSize, 0.0f }, { 0, 0, 1 }, { 0, 1 }, { 1, 0, 0 }, { 0, 1, 0 }, { 1, 1, 1, 1 } }, // Top-left
    // clang-format on
  };
  std::vector<uint32_t> indices = { 0, 1, 2, 2, 3, 0 };
  auto asset = std::make_shared<MeshAsset>(
    "Quad", std::move(vertices), std::move(indices));
  asset->CreateView("default", 0, asset->VertexCount(), 0, asset->IndexCount());
  return asset;
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
  if (render_controller_ && !render_items_.empty()
    && render_items_.front().mesh) {
    try {
      auto& registry = render_controller_->GetResourceRegistry();
      auto vertex_buffer
        = renderer_->GetVertexBuffer(*render_items_.front().mesh);
      registry.UnRegisterViews(*vertex_buffer);
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
  props.extent = { .width = 800, .height = 800 };
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

  // Update render context
  context_.renderer = renderer_.get();
  context_.render_controller = render_controller_.get();
}

auto MainModule::SetupFramebuffers() -> void
{
  CHECK_F(!gfx_weak_.expired());
  auto gfx = gfx_weak_.lock();

  auto fb_desc = graphics::FramebufferDesc {}.AddColorAttachment(
    surface_->GetCurrentBackBuffer());

  for (auto i = 0U; i < kFrameBufferCount; ++i) {
    auto desc = graphics::FramebufferDesc {}.AddColorAttachment(
      surface_->GetBackBuffer(i));

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
    graphics::ShaderType::kVertex, "FullScreenTriangle.hlsl"));

  const auto pixel_shader = gfx->GetShader(graphics::MakeShaderIdentifier(
    graphics::ShaderType::kPixel, "FullScreenTriangle.hlsl"));

  CHECK_NOTNULL_F(
    vertex_shader, "Failed to load FullScreenTriangle vertex shader");
  CHECK_NOTNULL_F(
    pixel_shader, "Failed to load FullScreenTriangle pixel shader");

  LOG_F(INFO, "Engine shaders loaded successfully");
}

// === Bindless Rendering Invariants ===
// - The engine manages a single shader-visible CBV_SRV_UAV heap per D3D12 type.
// - The CBV for per-draw constants is always at heap index 0 (register b0).
// - All other resources (SRVs, UAVs) are placed in the heap starting at
// index 1.
// - For this example, the vertex buffer SRV is always at heap index 1 (register
// t0, space0).
// - The constant buffer (CBV) contains a uint specifying the index of the
// vertex buffer SRV in the heap (always 1 for this draw).
// - The root signature and shader are designed to match this layout. See
// PipelineStateCache.cpp and FullScreenTriangle.hlsl for details.
auto MainModule::EnsureVertexBufferSrv() -> void
{
  if (!recreate_cbv_) {
    return;
  }

  auto& resource_registry = render_controller_->GetResourceRegistry();

  // Use the mesh from the first render item
  if (render_items_.empty() || !render_items_.front().mesh) {
    LOG_F(ERROR, "No mesh asset available for SRV registration");
    recreate_cbv_ = false;
    return;
  }
  const auto& mesh = render_items_.front().mesh;
  auto vertex_buffer = renderer_->GetVertexBuffer(*mesh);

  graphics::BufferViewDescription srv_view_desc;
  srv_view_desc.view_type = graphics::ResourceViewType::kStructuredBuffer_SRV;
  srv_view_desc.visibility = graphics::DescriptorVisibility::kShaderVisible;
  srv_view_desc.format = graphics::Format::kUnknown;
  srv_view_desc.stride = sizeof(Vertex);

  auto& descriptor_allocator = render_controller_->GetDescriptorAllocator();
  auto srv_handle = descriptor_allocator.Allocate(
    graphics::ResourceViewType::kStructuredBuffer_SRV,
    graphics::DescriptorVisibility::kShaderVisible);

  if (!srv_handle.IsValid()) {
    LOG_F(ERROR, "Failed to allocate descriptor handle for vertex buffer SRV!");
    recreate_cbv_ = false;
    return;
  }

  // Actually create the native view (SRV) in the backend
  const auto view = vertex_buffer->GetNativeView(srv_handle, srv_view_desc);

  vertex_srv_shader_visible_index_
    = descriptor_allocator.GetShaderVisibleIndex(srv_handle);

  resource_registry.RegisterView(
    *vertex_buffer, view, std::move(srv_handle), srv_view_desc);

  LOG_F(INFO, "Vertex buffer SRV registered at index {}",
    vertex_srv_shader_visible_index_);
}

auto MainModule::EnsureBindlessIndexingBuffer() -> void
{
  if (!recreate_cbv_) {
    // No need to create the constant buffer if we don't have a renderer or
    // if we don't need to recreate it
    return;
  }

  // Only create and update the buffer. No descriptor/view registration needed
  // for direct root CBV binding.
  if (!constant_buffer_) {
    DLOG_F(INFO, "Creating constant buffer for vertex buffer SRV index {}",
      vertex_srv_shader_visible_index_);
    graphics::BufferDesc cb_desc;
    cb_desc.size_bytes = 256; // D3D12 CBV alignment
    cb_desc.usage = graphics::BufferUsage::kConstant;
    cb_desc.memory = graphics::BufferMemory::kUpload;
    cb_desc.debug_name = "Vertex Buffer Index Constant Buffer";

    CHECK_F(!gfx_weak_.expired());
    const auto gfx = gfx_weak_.lock();

    constant_buffer_ = gfx->CreateBuffer(cb_desc);
    constant_buffer_->SetName("Vertex Buffer Index Constant Buffer");
  }

  // Always update the buffer contents (SRV index may change per frame)
  void* mapped_data = constant_buffer_->Map();
  memcpy(mapped_data, &vertex_srv_shader_visible_index_,
    sizeof(vertex_srv_shader_visible_index_));
  constant_buffer_->UnMap();

  recreate_cbv_ = false; // Reset the flag after creating/updating the CBV
}

auto MainModule::EnsureMeshDrawResources() -> void
{
  DCHECK_F(
    constant_buffer_ || recreate_cbv_, "Constant buffer must be created first");
  if (!constant_buffer_) {
    try {
      EnsureBindlessIndexingBuffer();
      recreate_cbv_ = true;
    } catch (const std::exception& e) {
      LOG_F(ERROR, "Error while ensuring CBV: {}", e.what());
      throw;
    }
  }

  DCHECK_F(
    constant_buffer_ != nullptr, "Constant buffer must be created first");
  try {
    EnsureVertexBufferSrv();
  } catch (const std::exception& e) {
    LOG_F(ERROR, "Error while ensuring vertex buffer SRV: {}", e.what());
    throw;
  }
  try {
    EnsureBindlessIndexingBuffer();
  } catch (const std::exception& e) {
    LOG_F(ERROR, "Error while ensuring CBV: {}", e.what());
    throw;
  }
}

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

  if (render_items_.empty()) {
    auto quad_mesh = MakeQuadMeshAsset();
    RenderItem quad_item {
      .mesh = quad_mesh,
      .material = nullptr,
      .world_transform
      = glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, -0.7f, 0.0f)),
      .normal_transform = glm::mat4(1.0f),
      .cast_shadows = false,
      .receive_shadows = false,
      .render_layer = 0u,
      .render_flags = 0u,
    };
    quad_item.UpdateComputedProperties();
    render_items_.push_back(quad_item);
  }

  // Ensure renderer has uploaded mesh resources
  if (!render_items_.empty() && render_items_.front().mesh) {
    renderer_->GetVertexBuffer(*render_items_.front().mesh);
    renderer_->GetIndexBuffer(*render_items_.front().mesh);
  }
  EnsureMeshDrawResources();

  auto gfx = gfx_weak_.lock();
  const auto recorder = render_controller_->AcquireCommandRecorder(
    graphics::SingleQueueStrategy().GraphicsQueueName(),
    "Main Window Command List");

  const auto fb = framebuffers_[render_controller_->CurrentFrameIndex()];
  fb->PrepareForRender(*recorder);
  recorder->BindFrameBuffer(*fb);

  // All viewport, scissors, and pipeline state setup is now handled by
  // ShaderPass

  context_.framebuffer = fb;

  // FIXME: better way to handle this?
  context_.opaque_draw_list
    = std::span<const RenderItem> { render_items_.data(),
        render_items_.size() };

  // --- ShaderPass integration ---
  static std::shared_ptr<engine::ShaderPass> shader_pass;
  static std::shared_ptr<engine::ShaderPassConfig> shader_pass_config;
  if (!shader_pass_config) {
    shader_pass_config = std::make_shared<engine::ShaderPassConfig>();
    shader_pass_config->color_texture = nullptr;
    shader_pass_config->clear_color
      = graphics::Color { 0.1f, 0.2f, 0.38f, 1.0f }; // Custom clear color
    shader_pass_config->enabled = true;
    shader_pass_config->debug_name = "ShaderPass";
  }
  if (!shader_pass) {
    shader_pass = std::make_shared<engine::ShaderPass>(shader_pass_config);
  }
  co_await shader_pass->PrepareResources(context_, *recorder);
  co_await shader_pass->Execute(context_, *recorder);

  co_return;
}
