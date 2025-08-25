//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <type_traits>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Data/ProceduralMeshes.h>
#include <Oxygen/Graphics/Common/Buffer.h>
#include <Oxygen/Graphics/Common/CommandQueue.h>
#include <Oxygen/Graphics/Common/CommandRecorder.h>
#include <Oxygen/Graphics/Common/DeferredObjectRelease.h>
#include <Oxygen/Graphics/Common/Graphics.h>
#include <Oxygen/Graphics/Common/PipelineState.h>
#include <Oxygen/Graphics/Common/Queues.h>
#include <Oxygen/Graphics/Common/RenderController.h>
#include <Oxygen/Graphics/Common/ResourceRegistry.h>
#include <Oxygen/Graphics/Common/Surface.h>
#include <Oxygen/Graphics/Common/Types/ResourceStates.h>
#include <Oxygen/Graphics/Direct3D12/Allocator/D3D12MemAlloc.h>
#include <Oxygen/Platform/Platform.h>
#include <Oxygen/Platform/Window.h>
#include <Oxygen/Renderer/RenderContext.h>
#include <Oxygen/Renderer/RenderItem.h>

#include <MainModule.h>

using oxygen::examples::MainModule;
using WindowProps = oxygen::platform::window::Properties;
using WindowEvent = oxygen::platform::window::Event;
using oxygen::engine::DepthPrePass;
using oxygen::engine::DepthPrePassConfig;
using oxygen::engine::RenderContext;
using oxygen::engine::RenderItem;
using oxygen::graphics::Buffer;
using oxygen::graphics::DeferredObjectRelease;
using oxygen::graphics::DepthStencilStateDesc;
using oxygen::graphics::Framebuffer;
using oxygen::graphics::RasterizerStateDesc;
using oxygen::graphics::ResourceStates;

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

  // FIXME: framebuffers need to be released before the renderer is gone

  surface_->DetachRenderer();
  renderer_.reset();
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
  surface_->AttachRenderer(renderer_);

  nursery_->Start([this]() -> co::Co<> {
    while (!window_weak_.expired() && !gfx_weak_.expired()) {
      const auto gfx = gfx_weak_.lock();
      co_await gfx->OnRenderStart();
      // Submit the render task to the renderer
      renderer_->Submit([this]() -> co::Co<> { co_await RenderScene(); });
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
  renderer_ = gfx->CreateRenderController(
    "Main Window Renderer", surface_, kFarmesInFlight - 1);
  CHECK_NOTNULL_F(renderer_, "Failed to create renderer for main window");
}

auto MainModule::RenderScene() -> co::Co<>
{
  if (gfx_weak_.expired()) {
    co_return;
  }

  if (framebuffers_.empty()) {
    try {
      SetupFramebuffers();
    } catch (const std::exception& e) {
      LOG_F(ERROR, "Failed to setup framebuffers: {}", e.what());
      co_return;
    }
  }

  if (!depth_pre_pass_) {
    try {
      SetupRenderPasses();
    } catch (const std::exception& e) {
      LOG_F(ERROR, "Failed to setup render passes: {}", e.what());
      co_return;
    }
  }

  // Update triangle transform if needed (e.g., rotation)

  auto gfx = gfx_weak_.lock();
  const auto recorder = renderer_->AcquireCommandRecorder(
    graphics::SingleQueueStrategy().GraphicsQueueName(),
    "Main Window Command List");

  // Select the correct framebuffer for the current frame
  const size_t frame_index = renderer_->CurrentFrameIndex();
  DCHECK_F(
    frame_index < framebuffers_.size(), "Invalid frame index: {}", frame_index);
  const auto fb = framebuffers_[frame_index];

  // 5. Prepare framebuffer, set viewport/scissors, pipeline, bindless, clear,
  // draw
  fb->PrepareForRender(*recorder);

  // Update the shared config for this frame
  const auto& fb_desc = fb->GetDescriptor();
  const auto depth_tex = fb_desc.depth_attachment.texture;
  depth_pre_pass_config_->depth_texture = depth_tex;
  depth_pre_pass_config_->framebuffer = fb;

  // Update the scene constants buffer (e.g., worldViewProjectionMatrix)
  if (constant_buffer_) {
    // Example: set identity matrix (or update with camera/transform as needed)
    struct SceneConstants {
      glm::mat4 worldViewProjectionMatrix;
    } constants;
    constants.worldViewProjectionMatrix
      = glm::mat4(1.0f); // Replace with actual transform if needed
    void* mapped = constant_buffer_->Map();
    memcpy(mapped, &constants, sizeof(constants));
    constant_buffer_->UnMap();
    depth_pre_pass_config_->scene_constants = constant_buffer_;
  } else {
    depth_pre_pass_config_->scene_constants = nullptr;
  }

  // Prepare and execute depth pre-pass
  RenderContext context {};
  context.opaque_draw_list
    = std::span<const RenderItem>(render_items_.data(), render_items_.size());
  context.framebuffer = fb;

  co_await depth_pre_pass_->PrepareResources(context, *recorder);
  co_await depth_pre_pass_->Execute(context, *recorder);

  co_return;
}

auto MainModule::SetupFramebuffers() -> void
{
  CHECK_F(!gfx_weak_.expired());
  const auto gfx = gfx_weak_.lock();

  framebuffers_.clear();

  // Create a unique depth texture for each frame in flight
  for (auto i = 0U; i < kFarmesInFlight; ++i) {
    graphics::TextureDesc depth_desc;
    depth_desc.width = 800;
    depth_desc.height = 800;
    depth_desc.format = Format::kDepth32;
    depth_desc.texture_type = graphics::TextureDimension::kTexture2D;
    depth_desc.is_shader_resource = true;
    depth_desc.is_render_target = true;
    depth_desc.use_clear_value = true;
    depth_desc.clear_value = { 1.0f, 0.0f, 0.0f, 0.0f };
    depth_desc.initial_state = ResourceStates::kDepthWrite;
    const auto depth_tex = gfx->CreateTexture(depth_desc);

    auto desc = graphics::FramebufferDesc {}
                  .AddColorAttachment(surface_->GetBackBuffer(i))
                  .SetDepthAttachment(depth_tex);
    framebuffers_.push_back(gfx->CreateFramebuffer(desc, *renderer_));
    CHECK_NOTNULL_F(
      framebuffers_[i], "Failed to create framebuffer for main window");
  }
}

auto MainModule::SetupRenderPasses() -> void
{
  // Create quad mesh asset using the Data system
  CHECK_F(!gfx_weak_.expired());
  const auto gfx = gfx_weak_.lock();

  // Generate a simple quad mesh asset (XY plane, 1x1)
  auto quad_mesh = data::MakeQuadMeshAsset(1.0f, 1.0f);
  CHECK_NOTNULL_F(quad_mesh, "Failed to create quad mesh asset");

  // TODO: Material asset
  // std::shared_ptr<oxygen::data::MaterialAsset> material = nullptr;

  // Set up world and normal transforms (identity for this example)
  constexpr auto world_transform = glm::mat4(1.0f);
  constexpr auto normal_transform = glm::mat3(1.0f);

  // Create RenderItem (data-driven, immutable)
  RenderItem quad_item {
    .mesh = quad_mesh,
    .material = nullptr, // TODO: replace with actual material asset
    .world_transform = world_transform,
    .normal_transform = normal_transform,
    .cast_shadows = false,
    .receive_shadows = false,
    .render_layer = 0u,
    .render_flags = 0u,
    // transformed bounding sphere and bounding boxes will be set by
    // UpdateComputedProperties
  };
  quad_item.UpdateComputedProperties();

  render_items_.clear();
  render_items_.push_back(std::move(quad_item));

  // Create constant buffer for scene constants if not already created
  if (!constant_buffer_) {
    graphics::BufferDesc cb_desc;
    cb_desc.size_bytes
      = sizeof(glm::mat4); // Only worldViewProjectionMatrix for now
    cb_desc.usage = graphics::BufferUsage::kConstant;
    cb_desc.memory = graphics::BufferMemory::kUpload;
    cb_desc.debug_name = "SceneConstantsBuffer";

    constant_buffer_ = gfx->CreateBuffer(cb_desc);
    constant_buffer_->SetName("SceneConstantsBuffer");
  }

  // Setup shared DepthPrePassConfig
  CHECK_F(!framebuffers_.empty());
  const auto first_fb = framebuffers_[0];
  const auto& fb_desc = first_fb->GetDescriptor();
  const auto depth_tex = fb_desc.depth_attachment.texture;
  depth_pre_pass_config_ = std::make_shared<DepthPrePassConfig>();
  depth_pre_pass_config_->depth_texture = depth_tex;
  depth_pre_pass_config_->framebuffer = first_fb;
  depth_pre_pass_config_->scene_constants = constant_buffer_;
  depth_pre_pass_config_->debug_name = "DepthPrePass";
  depth_pre_pass_ = std::make_unique<DepthPrePass>(depth_pre_pass_config_);
}
