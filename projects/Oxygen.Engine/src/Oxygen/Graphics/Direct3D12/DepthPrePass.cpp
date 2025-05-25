// filepath: f:\\projects\\DroidNet\\projects\\Oxygen.Engine\\src\\Oxygen\\Graphics\\DirectD3D12\\DepthPrePass.cpp
//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <functional> // For std::hash

#include <Oxygen/Base/Logging.h>
// Ensure d3d12::CommandRecorder is fully defined
#include <Oxygen/Graphics/Common/Buffer.h> // Added for BufferDesc, BufferUsage, BufferMemory
#include <Oxygen/Graphics/Common/CommandRecorder.h>
#include <Oxygen/Graphics/Common/DescriptorAllocator.h> // For DescriptorAllocator
#include <Oxygen/Graphics/Common/Detail/ResourceStateTracker.h>
#include <Oxygen/Graphics/Common/PipelineState.h> // For GraphicsPipelineDesc::Builder
#include <Oxygen/Graphics/Common/RenderItem.h>
#include <Oxygen/Graphics/Common/RenderItem.h> // Added for Vertex and RenderItem
#include <Oxygen/Graphics/Common/ResourceRegistry.h> // For ResourceRegistry
#include <Oxygen/Graphics/Common/Shaders.h> // For MakeShaderIdentifier
#include <Oxygen/Graphics/Common/Texture.h> // For TextureViewDescription
#include <Oxygen/Graphics/Direct3D12/CommandRecorder.h>
#include <Oxygen/Graphics/Direct3D12/DepthPrePass.h>
#include <Oxygen/Graphics/Direct3D12/Detail/FormatUtils.h>
#include <Oxygen/Graphics/Direct3D12/Detail/PipelineStateCache.h>
#include <Oxygen/Graphics/Direct3D12/Detail/dx12_utils.h>
#include <Oxygen/Graphics/Direct3D12/Detail/dx12_utils.h> // For CommandRecorder
#include <Oxygen/Graphics/Direct3D12/Graphics.h> // Kept for renderer_->GetGraphics() if needed
#include <Oxygen/Graphics/Direct3D12/Renderer.h> // Added Renderer include
#include <Oxygen/Graphics/Direct3D12/Texture.h> // For d3d12::Texture specific methods like GetDSV

using oxygen::graphics::d3d12::DepthPrePass;
using oxygen::graphics::detail::GetFormatInfo;

DepthPrePass::DepthPrePass(
    std::string_view name,
    const graphics::DepthPrePassConfig& config,
    d3d12::Renderer* renderer) // Changed parameter from graphics_context
    : Base(name, config)
    , renderer_(renderer)
    , last_built_pso_desc_(CreatePipelineStateDesc())
{
    DCHECK_NOTNULL_F(config_.depth_texture, "expecting a non-null depth texture");
}

auto DepthPrePass::NeedRebuildPipelineState() const -> bool
{
    // If pipeline state exists, check if depth texture properties have changed
    if (last_built_pso_desc_.FramebufferLayout().depth_stencil_format != GetDepthTexture().GetDescriptor().format) {
        return true;
    }
    if (last_built_pso_desc_.FramebufferLayout().sample_count != GetDepthTexture().GetDescriptor().sample_count) {
        return true;
    }
    return false; // No need to rebuild
}

oxygen::co::Co<> DepthPrePass::PrepareResources(graphics::CommandRecorder& command_recorder)
{
    DCHECK_NOTNULL_F(pipeline_state_,
        "Pipeline state should be built before PrepareResources is called for the first time.");

    // Check if we need to rebuild the pipeline state and the root signature.
    if (NeedRebuildPipelineState()) {
        last_built_pso_desc_ = CreatePipelineStateDesc();
    }

    // Call the base class's PrepareResources.
    co_await Base::PrepareResources(command_recorder);

    if (this->config_.framebuffer) {
        // D3D12 specific preparations for the optional framebuffer can be done here.
        // For example, if the framebuffer itself needs to transition its resources.
        // This depends on the d3d12::Framebuffer implementation.
    }

    co_return;
}

oxygen::co::Co<> DepthPrePass::Execute(graphics::CommandRecorder& command_recorder)
{
    DCHECK_F(!NeedRebuildPipelineState(), "Depth PSO should have been built by constructor or PrepareResources");

    auto& d3d12_recorder = static_cast<d3d12::CommandRecorder&>(command_recorder);

    // This will try to get a cached pipeline state or create a new one if needed.
    d3d12_recorder.SetPipelineState(last_built_pso_desc_); // It also sets the bindless root signature.
    d3d12_recorder.SetupBindlessRendering(); // This sets the bindless descriptor tables.

    auto dsv_handle = PrepareAndClearDepthStencilView(d3d12_recorder, GetDepthTexture());
    SetRenderTargetsAndViewport(d3d12_recorder, dsv_handle, GetDepthTexture());
    IssueDrawCalls(d3d12_recorder);

    co_return;
}

// --- Private helper implementations for Execute() ---

D3D12_CPU_DESCRIPTOR_HANDLE DepthPrePass::PrepareAndClearDepthStencilView(
    d3d12::CommandRecorder& d3d12_recorder,
    const graphics::Texture& depth_texture_ref)
{
    auto& registry = renderer_->GetResourceRegistry();
    auto& allocator = renderer_->GetDescriptorAllocator();

    D3D12_CPU_DESCRIPTOR_HANDLE d3d12_dsv_handle = {};

    // 1. Prepare TextureViewDescription
    const auto& depth_tex_desc = depth_texture_ref.GetDescriptor();
    graphics::TextureViewDescription dsv_view_desc {
        .view_type = graphics::ResourceViewType::kTexture_DSV,
        .visibility = graphics::DescriptorVisibility::kCpuOnly,
        .format = depth_tex_desc.format,
        .dimension = depth_tex_desc.dimension,
        .sub_resources = { // This is TextureSubResourceSet
            .base_mip_level = 0,
            .num_mip_levels = depth_tex_desc.mip_levels,
            .base_array_slice = 0,
            .num_array_slices = (depth_tex_desc.dimension == oxygen::graphics::TextureDimension::kTexture3D
                    ? depth_tex_desc.depth
                    : depth_tex_desc.array_size) },
        .is_read_only_dsv = false // Default for a writable DSV
    };

    // 2. Check with ResourceRegistry::FindView
    if (auto dsv = registry.Find(depth_texture_ref, dsv_view_desc); dsv.IsValid()) {
        // View found in registry
        d3d12_dsv_handle.ptr = dsv.AsInteger();
    } else {
        // View not found (cache miss), create and register it
        graphics::DescriptorHandle dsv_desc_handle = allocator.Allocate(
            graphics::ResourceViewType::kTexture_DSV,
            graphics::DescriptorVisibility::kCpuOnly);

        if (dsv_desc_handle.IsValid()) { // Corrected logic: proceed if handle is VALID
            // Register the newly created view
            auto dsv_native_object = registry.RegisterView(
                const_cast<graphics::Texture&>(depth_texture_ref), // Added const_cast
                std::move(dsv_desc_handle),
                dsv_view_desc);

            if (dsv_native_object.IsValid()) {
                d3d12_dsv_handle.ptr = dsv_native_object.AsInteger();
            } else {
                throw std::runtime_error(
                    "Failed to register DSV with resource registry even after successful allocation.");
            }
        } else { // Handle allocation failure
            throw std::runtime_error("Failed to allocate DSV descriptor handle for depth texture");
        }
    }

    // Clear the DSV
    const auto format_info = oxygen::graphics::detail::GetFormatInfo(depth_tex_desc.format);

    float clear_depth = (depth_tex_desc.use_clear_value && format_info.has_depth)
        ? depth_tex_desc.clear_value.r
        : 1.0f;

    uint8_t clear_stencil = (depth_tex_desc.use_clear_value && format_info.has_stencil)
        ? static_cast<uint8_t>(depth_tex_desc.clear_value.g) // Assuming stencil is in .g
        : 0;

    d3d12_recorder.ClearDepthStencilView(
        d3d12_dsv_handle,
        D3D12_CLEAR_FLAG_DEPTH | D3D12_CLEAR_FLAG_STENCIL,
        clear_depth,
        clear_stencil);

    return d3d12_dsv_handle;
}

void DepthPrePass::SetRenderTargetsAndViewport(
    d3d12::CommandRecorder& d3d12_recorder,
    D3D12_CPU_DESCRIPTOR_HANDLE dsv_handle,
    const graphics::Texture& depth_texture)
{
    d3d12_recorder.SetRenderTargets(0, nullptr, true, &dsv_handle);

    // Use the depth texture. It is already validated consistent with the
    // framebuffer if provided.
    const auto& common_tex_desc = depth_texture.GetDescriptor();
    auto width = common_tex_desc.width;
    auto height = common_tex_desc.height;

    graphics::ViewPort viewport {
        .top_left_x = 0.0f,
        .top_left_y = 0.0f,
        .width = static_cast<float>(width),
        .height = static_cast<float>(height),
        .min_depth = 0.0f,
        .max_depth = 1.0f
    };
    d3d12_recorder.SetViewport(viewport);

    graphics::Scissors scissors {
        .left = 0,
        .top = 0,
        .right = static_cast<int32_t>(width),
        .bottom = static_cast<int32_t>(height)
    };
    d3d12_recorder.SetScissors(scissors);

    d3d12_recorder.IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
}

void DepthPrePass::IssueDrawCalls(d3d12::CommandRecorder& d3d12_recorder)
{
    // Note on D3D12 Upload Heap Resource States:
    // Buffers created on D3D12_HEAP_TYPE_UPLOAD (like these temporary vertex buffers)
    // are typically implicitly in a state (D3D12_RESOURCE_STATE_GENERIC_READ)
    // that allows them to be read by the GPU after CPU writes without explicit
    // state transition barriers. Thus, explicit CommandRecorder::RequireResourceState
    // calls are often not strictly needed for these specific transient resources on D3D12.
    // The Renderer's DeferRelease mechanism will ensure they are kept alive until
    // the GPU is finished.

    for (const auto* item : GetDrawList()) {
        DCHECK_NOTNULL_F(item);

        if (item->vertex_count == 0) {
            continue; // Nothing to draw
        }

        // Validate RenderItem data consistency
        if (item->vertices.empty() || item->vertex_count > item->vertices.size()) {
            DLOG_F(WARNING, "DepthPrePass::IssueDrawCalls: RenderItem has inconsistent vertex data. "
                            "vertex_count: {}, vertices.size(): {}. Skipping item.",
                item->vertex_count, item->vertices.size());
            continue;
        }

        const uint32_t num_vertices_to_draw = item->vertex_count;
        const size_t data_size_bytes = num_vertices_to_draw * sizeof(graphics::Vertex);

        // 1. Create a temporary upload buffer for the vertex data
        graphics::BufferDesc vb_upload_desc;
        vb_upload_desc.size_bytes = data_size_bytes;
        vb_upload_desc.usage = graphics::BufferUsage::kVertex; // Corrected from kVertex
        vb_upload_desc.memory = graphics::BufferMemory::kUpload;
        vb_upload_desc.debug_name = "DepthPrePass_TempVB";

        auto temp_vb = renderer_->CreateBuffer(vb_upload_desc);
        DCHECK_NOTNULL_F(temp_vb, "Failed to create temporary vertex buffer for DepthPrePass");
        if (!temp_vb) {
            LOG_F(ERROR, "DepthPrePass::IssueDrawCalls: Failed to create temporary vertex buffer. Skipping item.");
            continue;
        }

        // The renderer will manage the lifetime of this temporary buffer until the GPU is done.
        DeferredObjectRelease(temp_vb, renderer_->GetPerFrameResourceManager());

        // 2. Update the buffer with vertex data.
        // The Buffer::Update method for an kUpload buffer should handle mapping & copying.
        temp_vb->Update(item->vertices.data(), data_size_bytes, 0);

        // 3. Bind the vertex buffer using the abstract CommandRecorder interface
        std::shared_ptr<graphics::Buffer> buffer_array[] = { temp_vb };
        uint32_t stride_array[] = { static_cast<uint32_t>(sizeof(graphics::Vertex)) };
        uint32_t offset_array[] = { 0 }; // Start offset within each buffer is 0

        d3d12_recorder.SetVertexBuffers(
            1, // num: number of vertex buffers to bind
            buffer_array,
            stride_array,
            offset_array);

        // 4. Issue the draw call
        d3d12_recorder.Draw(
            num_vertices_to_draw, // VertexCountPerInstance
            1, // InstanceCount
            0, // StartVertexLocation
            0 // StartInstanceLocation
        );
    }
}

auto DepthPrePass::CreatePipelineStateDesc() -> graphics::GraphicsPipelineDesc
{
    graphics::RasterizerStateDesc raster_desc {
        .fill_mode = graphics::FillMode::kSolid,
        .cull_mode = graphics::CullMode::kBack,
        .front_counter_clockwise = false,
        // D3D12_RASTERIZER_DESC::MultisampleEnable is for controlling anti-aliasing behavior on lines and edges,
        // not strictly for enabling/disabling MSAA sample processing for a texture.
        // The sample_count in FramebufferLayoutDesc and the texture itself dictate MSAA.
        // It's often left false unless specific line/edge AA is needed.
        // Let's rely on FramebufferLayoutDesc::sample_count for MSAA control.
        .multisample_enable = false // Or depth_texture.GetDesc().sample_count > 1 if specifically needed for rasterizer stage
    };

    DepthStencilStateDesc ds_desc {
        .depth_test_enable = true, // Enable depth testing
        .depth_write_enable = true, // Enable writing to depth buffer
        .depth_func = CompareOp::kLessOrEqual, // Typical depth comparison function
        .stencil_enable = false, // Stencil testing usually disabled unless required
        .stencil_read_mask = 0xFF, // Default full-mask value for reading stencil buffer
        .stencil_write_mask = 0xFF // Default full-mask value for writing to stencil buffer
    };

    auto& depth_texture_desc = GetDepthTexture().GetDescriptor();
    graphics::FramebufferLayoutDesc fb_layout_desc {
        .color_target_formats = {},
        .depth_stencil_format = depth_texture_desc.format,
        .sample_count = depth_texture_desc.sample_count
    };

    return graphics::GraphicsPipelineDesc::Builder()
        .SetVertexShader(graphics::ShaderStageDesc {
            .shader = graphics::MakeShaderIdentifier(graphics::ShaderType::kVertex, "DepthOnlyVS.hlsl") })
        .SetPixelShader(graphics::ShaderStageDesc {
            .shader = graphics::MakeShaderIdentifier(graphics::ShaderType::kPixel, "MinimalPS.hlsl") })
        .SetPrimitiveTopology(graphics::PrimitiveType::kTriangleList)
        .SetRasterizerState(raster_desc)
        .SetDepthStencilState(ds_desc)
        .SetBlendState({})
        .SetFramebufferLayout(fb_layout_desc)
        .Build();
}
