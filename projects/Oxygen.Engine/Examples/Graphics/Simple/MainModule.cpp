//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <type_traits>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

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
#include <Oxygen/Renderer/RenderItem.h>
#include <Oxygen/Renderer/Renderer.h>
#include <Oxygen/Renderer/ShaderPass.h>

#include "./MainModule.h"

using oxygen::examples::MainModule;
using WindowProps = oxygen::platform::window::Properties;
using WindowEvent = oxygen::platform::window::Event;
using oxygen::data::Mesh;
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
}

auto MainModule::SetupFramebuffers() -> void
{
  CHECK_F(!gfx_weak_.expired());
  auto gfx = gfx_weak_.lock();

  for (auto i = 0U; i < kFrameBufferCount; ++i) {
    graphics::TextureDesc depth_desc;
    depth_desc.width = 800;
    depth_desc.height = 800;
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

// === Bindless Rendering Invariants ===
// - The engine manages a single shader-visible CBV_SRV_UAV heap per D3D12 type.
// - The Resource Indices CBV for bindless rendering is always (register b0).
// - All other resources (SRVs, UAVs) are placed in the heap starting at
// index 1.
// Removed: indices buffer creation/upload now handled by Renderer.

auto MainModule::ExtractMaterialConstants(
  const data::MaterialAsset& material) const -> engine::MaterialConstants
{
  engine::MaterialConstants constants;

  // Extract base color (RGBA)
  const auto base_color_span = material.GetBaseColor();
  constants.base_color = glm::vec4(base_color_span[0], base_color_span[1],
    base_color_span[2], base_color_span[3]);

  // Extract PBR scalar values
  constants.metalness = material.GetMetalness();
  constants.roughness = material.GetRoughness();
  constants.normal_scale = material.GetNormalScale();
  constants.ambient_occlusion = material.GetAmbientOcclusion();

  // Extract texture indices
  // For now, set all texture indices to invalid since we don't have texture
  // loading yet In a full implementation, these would be resolved to actual
  // descriptor heap indices
  constexpr uint32_t kInvalidTextureIndex
    = 0; // 0 typically represents an invalid or default texture
  constants.base_color_texture_index = kInvalidTextureIndex;
  constants.normal_texture_index = kInvalidTextureIndex;
  constants.metallic_texture_index = kInvalidTextureIndex;
  constants.roughness_texture_index = kInvalidTextureIndex;
  constants.ambient_occlusion_texture_index = kInvalidTextureIndex;

  // Extract material flags
  constants.flags = material.GetFlags();

  return constants;
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

  if (renderer_ && renderer_->GetOpaqueItems().empty()) {
    const std::shared_ptr<data::Mesh> cube_mesh
      = data::GenerateMesh("Cube/TestMesh", {});
    // Create a default material for the cube
    const auto cube_material = data::MaterialAsset::CreateDebug();
    RenderItem cube_item {
      .mesh = cube_mesh,
      .material = cube_material,
      .world_transform = glm::mat4(1.0F), // Not used directly in shader
      .normal_transform = glm::mat4(1.0F),
      .cast_shadows = false,
      .receive_shadows = false,
      .render_layer = 0u,
      .render_flags = 0u,
    };
    // Container validates and recomputes
    renderer_->OpaqueItems().Add(cube_item);

    // Add a second mesh: an offset copy of the cube so they don't overlap.
    // Shaders currently treat object space == world space, so bake translation.
    {
      using oxygen::data::MeshBuilder;
      using oxygen::data::detail::IndexType;
      using oxygen::data::pak::MeshViewDesc;

      const auto vspan = cube_mesh->Vertices();
      std::vector<Vertex> translated_vertices(vspan.begin(), vspan.end());
      const glm::vec3 offset
        = { 2.5F, 0.0F, 0.0F }; // Closer separation for better framing
      for (auto& v : translated_vertices) {
        v.position += offset;
      }

      // Debug: Log a few vertex positions to verify offset
      DLOG_F(INFO, "Second cube vertex positions:");
      for (size_t i = 0; i < (std::min)(size_t(4), translated_vertices.size());
        ++i) {
        const auto& pos = translated_vertices[i].position;
        DLOG_F(INFO, "  Vertex[{}]: ({:.2F}, {:.2F}, {:.2F})", i, pos.x, pos.y,
          pos.z);
      }

      std::vector<uint32_t> indices32;
      const auto ib = cube_mesh->IndexBuffer();
      if (ib.type == IndexType::kUInt16) {
        auto src = ib.AsU16();
        indices32.resize(src.size());
        for (size_t i = 0; i < src.size(); ++i)
          indices32[i] = src[i];
      } else if (ib.type == IndexType::kUInt32) {
        auto src = ib.AsU32();
        indices32.assign(src.begin(), src.end());
      }

      auto offset_mesh
        = MeshBuilder(0, "Cube/OffsetMesh")
            .WithVertices(std::move(translated_vertices))
            .WithIndices(std::move(indices32))
            .BeginSubMesh("default", data::MaterialAsset::CreateDefault())
            .WithMeshView(MeshViewDesc {
              .first_index = 0,
              .index_count = static_cast<uint32_t>(ib.Count()),
              .first_vertex = 0,
              .vertex_count = static_cast<uint32_t>(cube_mesh->VertexCount()),
            })
            .EndSubMesh()
            .Build();

      const auto cube2_material = data::MaterialAsset::CreateDebug();
      RenderItem cube2_item {
        .mesh = std::move(offset_mesh),
        .material = cube2_material,
        .world_transform = glm::mat4(1.0F),
        .normal_transform = glm::mat4(1.0F),
        .cast_shadows = false,
        .receive_shadows = false,
        .render_layer = 0u,
        .render_flags = 0u,
      };
      renderer_->OpaqueItems().Add(std::move(cube2_item));
    }

    // One-time info log: items were just added to the container.
    const auto item_count = renderer_->GetOpaqueItems().size();
    LOG_F(
      INFO, "Initial render items added to container (count: {})", item_count);
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

  // Rotation removed temporarily (object space == world space in shaders).

  // Look from an angle to see both cubes clearly with closer positioning
  glm::vec3 camera_position = glm::vec3(
    1.25F, 1.5F, -5.0F); // Much closer to the cubes, centered between them
  glm::vec3 target
    = glm::vec3(1.25F, 0.0F, 0.0F); // Look at center between cubes (0 and 2.5)
  auto up = glm::vec3(0, 1, 0);

  const float aspect = static_cast<float>(surface_->Width())
    / static_cast<float>(surface_->Height());
  if (renderer_) {
    renderer_->ModifySceneConstants([=](auto& sc) {
      sc.SetViewMatrix(glm::lookAt(camera_position, target, up))
        .SetProjectionMatrix(glm::perspective(
          glm::radians(45.0F), // Narrower field of view for better framing
          aspect, // aspect
          0.1F, // zNear
          600.0F // zFar
          ))
        .SetCameraPosition(glm::vec3 { 0.0F, 0.0F, -3.5F });
    });
  }

  // Update material constants from the first render item's material
  if (renderer_) {
    const auto items = renderer_->GetOpaqueItems();
    if (!items.empty() && items.front().material) {
      const auto material_constants
        = ExtractMaterialConstants(*items.front().material);
      renderer_->SetMaterialConstants(material_constants);
    }
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
