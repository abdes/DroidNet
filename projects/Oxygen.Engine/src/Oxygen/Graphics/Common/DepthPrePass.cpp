//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <stdexcept>
#include <utility>

#include <fmt/format.h>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Base/NoStd.h>
#include <Oxygen/Core/ViewPort.h>
#include <Oxygen/Graphics/Common/Buffer.h>
#include <Oxygen/Graphics/Common/CommandRecorder.h>
#include <Oxygen/Graphics/Common/DeferredObjectRelease.h>
#include <Oxygen/Graphics/Common/DepthPrepass.h>
#include <Oxygen/Graphics/Common/DescriptorAllocator.h>
#include <Oxygen/Graphics/Common/Framebuffer.h>
#include <Oxygen/Graphics/Common/Graphics.h>
#include <Oxygen/Graphics/Common/RenderController.h>
#include <Oxygen/Graphics/Common/RenderItem.h>
#include <Oxygen/Graphics/Common/ResourceRegistry.h>
#include <Oxygen/Graphics/Common/Texture.h>
#include <Oxygen/Graphics/Common/Types/Color.h>
#include <Oxygen/Graphics/Common/Types/ResourceStates.h>
#include <Oxygen/Graphics/Common/Types/Scissors.h>

using oxygen::ViewPort;
using oxygen::graphics::DepthPrePass;

DepthPrePass::DepthPrePass(
  RenderController* renderer, std::shared_ptr<Config> config)
  : RenderPass(config->debug_name)
  , config_(std::move(config))
  , renderer_(renderer)
  , last_built_pso_desc_(DepthPrePass::CreatePipelineStateDesc())
{
  DCHECK_NOTNULL_F(renderer_, "RenderController must not be null.");
  DepthPrePass::ValidateConfig();
}

void DepthPrePass::SetViewport(const oxygen::ViewPort& viewport)
{
  if (!viewport.IsValid()) {
    throw std::invalid_argument(
      fmt::format("viewport {} is invalid", nostd::to_string(viewport)));
  }
  DCHECK_NOTNULL_F(
    config_->depth_texture, "expecting a non-null depth texture");

  const auto& tex_desc = config_->depth_texture->GetDescriptor();

  auto viewport_width = viewport.top_left_x + viewport.width;
  auto viewport_height = viewport.top_left_y + viewport.height;
  if (viewport_width > static_cast<float>(tex_desc.width)
    || viewport_height > static_cast<float>(tex_desc.height)) {
    throw std::out_of_range(fmt::format(
      "viewport dimensions ({}, {}) exceed depth_texture bounds: ({}, {})",
      viewport_width, viewport_height, tex_desc.width, tex_desc.height));
  }
  viewport_.emplace(viewport);
}

void DepthPrePass::SetScissors(const Scissors& scissors)
{
  if (!scissors.IsValid()) {
    throw std::invalid_argument(
      fmt::format("scissors {} are invalid.", nostd::to_string(scissors)));
  }
  DCHECK_NOTNULL_F(config_->depth_texture,
    "expecting depth texture to be valid when setting scissors");

  const auto& tex_desc = config_->depth_texture->GetDescriptor();

  // Assuming scissors coordinates are relative to the texture origin (0,0)
  if (scissors.left < 0 || scissors.top < 0) {
    throw std::out_of_range("scissors left and top must be non-negative.");
  }
  if (std::cmp_greater(scissors.right, tex_desc.width)
    || std::cmp_greater(scissors.bottom, tex_desc.height)) {
    throw std::out_of_range(fmt::format(
      "scissors dimensions ({}, {}) exceed depth_texture bounds ({}, {})",
      scissors.right, scissors.bottom, tex_desc.width, tex_desc.height));
  }

  scissors_.emplace(scissors);
}

void DepthPrePass::SetClearColor(const Color& color)
{
  clear_color_.emplace(color);
}

void DepthPrePass::SetEnabled(const bool enabled) { enabled_ = enabled; }

auto DepthPrePass::IsEnabled() const -> bool { return enabled_; }

auto DepthPrePass::PrepareResources(CommandRecorder& recorder) -> co::Co<>
{
  // Check if we need to rebuild the pipeline state and the root signature.
  if (NeedRebuildPipelineState()) {
    last_built_pso_desc_ = CreatePipelineStateDesc();
  }

  // Ensure the depth_texture is in kDepthWrite state before derived classes
  // might perform operations like clears. Note that the depth_texture should
  // be already in a valid state when this method is called, but we are
  // explicitly transitioning it for safety. The transition will be optimized
  // out if the state is already correct.
  if (config_->depth_texture) {
    recorder.RequireResourceState(
      *config_->depth_texture, ResourceStates::kDepthWrite);
    recorder.FlushBarriers();
  }

  co_return;
}

void DepthPrePass::ValidateConfig()
{
  if (!config_ || !config_->depth_texture) {
    throw std::runtime_error(
      "invalid DepthPrePassConfig: depth_texture cannot be null.");
  }
  if (!config_->scene_constants) {
    throw std::runtime_error(
      "invalid DepthPrePassConfig: scene_constants cannot be null.");
  }
  if (config_->framebuffer) {
    const auto& fb_desc = config_->framebuffer->GetDescriptor();
    if (fb_desc.depth_attachment.texture
      && fb_desc.depth_attachment.texture != config_->depth_texture) {
      throw std::runtime_error(
        "invalid DepthPrePassConfig: framebuffer depth attachment "
        "texture must match depth_texture when both are provided and "
        "framebuffer has a depth attachment.");
    }
  }
  // Backend-specific derived classes can override this to add more checks.
}

auto DepthPrePass::NeedRebuildPipelineState() const -> bool
{
  // If pipeline state exists, check if depth texture properties have changed
  if (last_built_pso_desc_.FramebufferLayout().depth_stencil_format
    != GetDepthTexture().GetDescriptor().format) {
    return true;
  }
  if (last_built_pso_desc_.FramebufferLayout().sample_count
    != GetDepthTexture().GetDescriptor().sample_count) {
    return true;
  }
  return false; // No need to rebuild
}

void DepthPrePass::PrepareSceneConstantsBuffer(
  CommandRecorder& command_recorder) const
{
  const auto& root_param = last_built_pso_desc_.RootBindings()[2];
  DCHECK_F(std::holds_alternative<DirectBufferBinding>(root_param.data),
    "Expected root parameter 1's data to be DirectBufferBinding");

  // Bind the buffer as a root CBV (direct GPU virtual address)
  command_recorder.SetGraphicsRootConstantBufferView(
    root_param.GetRootParameterIndex(), // binding 2 (b1, space0)
    config_->scene_constants->GetGPUVirtualAddress());
}

auto DepthPrePass::Execute(CommandRecorder& command_recorder) -> co::Co<>
{
  DCHECK_F(!NeedRebuildPipelineState(),
    "Depth PSO should have been built by constructor or PrepareResources");

  LOG_SCOPE_F(2, "DepthPrePass::Execute");

  // This will try to get a cached pipeline state or create a new one if needed.
  command_recorder.SetPipelineState(
    last_built_pso_desc_); // It also sets the bindless root signature.

  try {
    const auto dsv = PrepareDepthStencilView(GetDepthTexture());
    DCHECK_F(dsv.IsValid(), "DepthStencilView must be valid after preparation");

    ClearDepthStencilView(command_recorder, dsv);
    SetViewAsRenderTarget(command_recorder, dsv);
    SetupViewPortAndScissors(command_recorder);
    PrepareSceneConstantsBuffer(command_recorder);
    IssueDrawCalls(command_recorder);
  } catch (const std::exception& e) {
    DLOG_F(ERROR, "DepthPrePass::Execute failed: %s", e.what());
    throw; // Re-throw to propagate the error
  }

  co_return;
}

// --- Private helper implementations for Execute() ---

auto DepthPrePass::PrepareDepthStencilView(const Texture& depth_texture_ref)
  -> NativeObject
{
  auto& registry = renderer_->GetResourceRegistry();
  auto& allocator = renderer_->GetDescriptorAllocator();

  // 1. Prepare TextureViewDescription
  const auto& depth_tex_desc = depth_texture_ref.GetDescriptor();
  const TextureViewDescription dsv_view_desc {
        .view_type = ResourceViewType::kTexture_DSV,
        .visibility = DescriptorVisibility::kCpuOnly,
        .format = depth_tex_desc.format,
        .dimension = depth_tex_desc.dimension,
        .sub_resources = { // This is TextureSubResourceSet
            .base_mip_level = 0,
            .num_mip_levels = depth_tex_desc.mip_levels,
            .base_array_slice = 0,
            .num_array_slices = (depth_tex_desc.dimension == TextureDimension::kTexture3D
                    ? depth_tex_desc.depth
                    : depth_tex_desc.array_size) },
        .is_read_only_dsv = false // Default for a writable DSV
    };

  // 2. Check with ResourceRegistry::FindView
  if (const auto dsv = registry.Find(depth_texture_ref, dsv_view_desc);
    dsv.IsValid()) {
    return dsv;
  }
  // View not found (cache miss), create and register it
  DescriptorHandle dsv_desc_handle = allocator.Allocate(
    ResourceViewType::kTexture_DSV, DescriptorVisibility::kCpuOnly);

  if (!dsv_desc_handle.IsValid()) {
    throw std::runtime_error(
      "Failed to allocate DSV descriptor handle for depth texture");
  }
  // Register the newly created view
  const auto dsv = registry.RegisterView(
    const_cast<Texture&>(depth_texture_ref), // Added const_cast
    std::move(dsv_desc_handle), dsv_view_desc);

  if (!dsv.IsValid()) {
    throw std::runtime_error("Failed to register DSV with resource registry "
                             "even after successful allocation.");
  }

  return dsv;
}

void DepthPrePass::ClearDepthStencilView(
  CommandRecorder& command_recorder, const NativeObject& dsv_handle) const
{
  command_recorder.ClearDepthStencilView(GetDepthTexture(), dsv_handle,
    ClearFlags::kDepth, // only depth, as the depth pre-pass does not use the
                        // stencil buffer
    1.0f, 0);
}

void DepthPrePass::SetViewAsRenderTarget(
  CommandRecorder& command_recorder, const NativeObject& dsv) const
{
  DCHECK_F(dsv.IsValid(),
    "DepthStencilView must be valid before setting render targets");

  command_recorder.SetRenderTargets({}, dsv);
}

void DepthPrePass::SetupViewPortAndScissors(
  CommandRecorder& command_recorder) const
{
  // Use the depth texture. It is already validated consistent with the
  // framebuffer if provided.
  const auto& common_tex_desc = GetDepthTexture().GetDescriptor();
  const auto width = common_tex_desc.width;
  const auto height = common_tex_desc.height;

  const ViewPort viewport { .top_left_x = 0.0f,
    .top_left_y = 0.0f,
    .width = static_cast<float>(width),
    .height = static_cast<float>(height),
    .min_depth = 0.0f,
    .max_depth = 1.0f };
  command_recorder.SetViewport(viewport);

  const Scissors scissors { .left = 0,
    .top = 0,
    .right = static_cast<int32_t>(width),
    .bottom = static_cast<int32_t>(height) };
  command_recorder.SetScissors(scissors);
}

void DepthPrePass::IssueDrawCalls(CommandRecorder& command_recorder) const
{
  // Note on D3D12 Upload Heap Resource States:
  // Buffers created on D3D12_HEAP_TYPE_UPLOAD (like these temporary vertex
  // buffers) are typically implicitly in a state
  // (D3D12_RESOURCE_STATE_GENERIC_READ) that allows them to be read by the GPU
  // after CPU writes without explicit state transition barriers. Thus, explicit
  // CommandRecorder::RequireResourceState calls are often not strictly needed
  // for these specific transient resources on D3D12. The RenderController
  // "Deferred Release" mechanism will ensure they are kept alive until the GPU
  // is finished.

  for (const auto* item : GetDrawList()) {
    DCHECK_NOTNULL_F(item);

    if (item->vertex_count == 0) {
      continue; // Nothing to draw
    }

    // Validate RenderItem data consistency
    if (item->vertices.empty() || item->vertex_count > item->vertices.size()) {
      DLOG_F(WARNING,
        "DepthPrePass::IssueDrawCalls: RenderItem has inconsistent vertex "
        "data. "
        "vertex_count: {}, vertices.size(): {}. Skipping item.",
        item->vertex_count, item->vertices.size());
      continue;
    }

    const uint32_t num_vertices_to_draw = item->vertex_count;
    const size_t data_size_bytes = num_vertices_to_draw * sizeof(Vertex);

    // 1. Create a temporary upload buffer for the vertex data
    BufferDesc vb_upload_desc;
    vb_upload_desc.size_bytes = data_size_bytes;
    vb_upload_desc.usage = BufferUsage::kVertex; // Corrected from kVertex
    vb_upload_desc.memory = BufferMemory::kUpload;
    vb_upload_desc.debug_name = "DepthPrePass_TempVB";

    auto temp_vb = renderer_->GetGraphics().CreateBuffer(vb_upload_desc);
    DCHECK_NOTNULL_F(
      temp_vb, "Failed to create temporary vertex buffer for DepthPrePass");
    if (!temp_vb) {
      LOG_F(ERROR,
        "DepthPrePass::IssueDrawCalls: Failed to create temporary vertex "
        "buffer. Skipping item.");
      continue;
    }

    // 2. Update the buffer with vertex data.
    // The Buffer::Update method for an kUpload buffer should handle mapping &
    // copying.
    temp_vb->Update(item->vertices.data(), data_size_bytes, 0);

    // 3. Bind the vertex buffer using the abstract CommandRecorder interface
    const std::shared_ptr<Buffer> buffer_array[1] = { temp_vb };
    constexpr uint32_t stride_array[1]
      = { static_cast<uint32_t>(sizeof(Vertex)) };

    command_recorder.SetVertexBuffers(
      1, // num: number of vertex buffers to bind
      buffer_array, stride_array);

    // 4. Issue the draw call
    command_recorder.Draw(num_vertices_to_draw, // VertexCountPerInstance
      1, // InstanceCount
      0, // StartVertexLocation
      0 // StartInstanceLocation
    );

    // The renderer will manage the lifetime of this temporary buffer until the
    // GPU is done.
    DeferredObjectRelease(temp_vb, renderer_->GetPerFrameResourceManager());
  }
}

auto DepthPrePass::CreatePipelineStateDesc() -> GraphicsPipelineDesc
{
  constexpr RasterizerStateDesc raster_desc { .fill_mode = FillMode::kSolid,
    .cull_mode = CullMode::kBack,
    .front_counter_clockwise = true, // Default winding order for front faces

    // D3D12_RASTERIZER_DESC::MultisampleEnable is for controlling
    // anti-aliasing behavior on lines and edges, not strictly for
    // enabling/disabling MSAA sample processing for a texture. The
    // sample_count in FramebufferLayoutDesc and the texture itself dictate
    // MSAA. It's often left false unless specific line/edge AA is needed.
    //
    // Or `depth_texture.GetDesc().sample_count > 1` if specifically needed
    // for rasterizer stage
    .multisample_enable = false };

  constexpr DepthStencilStateDesc ds_desc {
    .depth_test_enable = true, // Enable depth testing
    .depth_write_enable = true, // Enable writing to depth buffer
    .depth_func = CompareOp::kLessOrEqual, // Typical depth comparison function
    .stencil_enable = false, // Stencil testing usually disabled unless required
    .stencil_read_mask
    = 0xFF, // Default full-mask value for reading stencil buffer
    .stencil_write_mask
    = 0xFF // Default full-mask value for writing to stencil buffer
  };

  auto& depth_texture_desc = GetDepthTexture().GetDescriptor();
  const FramebufferLayoutDesc fb_layout_desc { .color_target_formats = {},
    .depth_stencil_format = depth_texture_desc.format,
    .sample_count = depth_texture_desc.sample_count };

  constexpr RootBindingDesc srv_table_desc {
        // t0, space0
        .binding_slot_desc = BindingSlotDesc {
            .register_index = 0,
            .register_space = 0 },
        .visibility = ShaderStageFlags::kAll,
        .data = DescriptorTableBinding {
            .view_type = ResourceViewType::kStructuredBuffer_SRV,
            .base_index = 0 // unbounded
        }
    };

  constexpr RootBindingDesc resource_indices_cbv_desc { // b0, space0
    .binding_slot_desc
    = BindingSlotDesc { .register_index = 0, .register_space = 0 },
    .visibility = ShaderStageFlags::kAll,
    .data = DirectBufferBinding {}
  };

  constexpr RootBindingDesc scene_constants_cbv_desc { // b1, space0
    .binding_slot_desc
    = BindingSlotDesc { .register_index = 1, .register_space = 0 },
    .visibility = ShaderStageFlags::kAll,
    .data = DirectBufferBinding {}
  };

  return GraphicsPipelineDesc::Builder()
    .SetVertexShader(ShaderStageDesc { .shader
      = MakeShaderIdentifier(ShaderType::kVertex, "DepthPrePass.hlsl") })
    .SetPixelShader(ShaderStageDesc {
      .shader = MakeShaderIdentifier(ShaderType::kPixel, "DepthPrePass.hlsl") })
    .SetPrimitiveTopology(PrimitiveType::kTriangleList)
    .SetRasterizerState(raster_desc)
    .SetDepthStencilState(ds_desc)
    .SetBlendState({})
    .SetFramebufferLayout(fb_layout_desc)
    .AddRootBinding(RootBindingItem(srv_table_desc)) // binding 0: SRV table
    .AddRootBinding(RootBindingItem(
      resource_indices_cbv_desc)) // binding 1: ResourceIndices CBV (b0)
    .AddRootBinding(RootBindingItem(
      scene_constants_cbv_desc)) // binding 2: SceneConstants CBV (b1)
    .Build();
}
