//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <span>

#include <Oxygen/Graphics/Common/CommandRecorder.h>
#include <Oxygen/Graphics/Common/DeferredObjectRelease.h>
#include <Oxygen/Graphics/Common/Types/Color.h>
#include <Oxygen/Graphics/Direct3D12/CommandList.h>
#include <Oxygen/Graphics/Direct3D12/Detail/Types.h>
#include <Oxygen/Graphics/Direct3D12/api_export.h>

#include <wrl/client.h>

namespace oxygen::graphics::d3d12 {

namespace detail {

    struct ShaderVisibleHeapInfo {
        D3D12_DESCRIPTOR_HEAP_TYPE heap_type;
        ID3D12DescriptorHeap* heap;
        D3D12_GPU_DESCRIPTOR_HANDLE gpu_handle;

        ShaderVisibleHeapInfo(
            D3D12_DESCRIPTOR_HEAP_TYPE type,
            ID3D12DescriptorHeap* heap,
            D3D12_GPU_DESCRIPTOR_HANDLE gpu_handle)
            : heap_type(type)
            , heap(heap)
            , gpu_handle(gpu_handle)
        {
        }
    };

} // namespace detail

class Renderer;

class CommandRecorder final : public graphics::CommandRecorder {
    using Base = graphics::CommandRecorder;

public:
    CommandRecorder(
        Renderer* renderer,
        graphics::CommandList* command_list,
        graphics::CommandQueue* target_queue);

    OXYGEN_D3D12_API ~CommandRecorder() override;

    OXYGEN_MAKE_NON_COPYABLE(CommandRecorder);
    OXYGEN_MAKE_NON_MOVABLE(CommandRecorder);

    void Begin() override;
    auto End() -> graphics::CommandList* override;

    void SetPipelineState(GraphicsPipelineDesc desc) override;
    void SetPipelineState(ComputePipelineDesc desc) override;

    void SetupBindlessRendering() override;

    void SetViewport(const ViewPort& viewport) override;
    void SetScissors(const Scissors& scissors) override;

    void SetVertexBuffers(uint32_t num, const std::shared_ptr<graphics::Buffer>* vertex_buffers, const uint32_t* strides, const uint32_t* offsets) override;
    void Draw(uint32_t vertex_num, uint32_t instances_num, uint32_t vertex_offset, uint32_t instance_offset) override;
    void DrawIndexed(uint32_t index_num, uint32_t instances_num, uint32_t index_offset, int32_t vertex_offset, uint32_t instance_offset) override;

    void BindFrameBuffer(const graphics::Framebuffer& framebuffer) override;
    void ClearFramebuffer(
        const oxygen::graphics::Framebuffer& framebuffer,
        std::optional<std::vector<std::optional<Color>>> color_clear_values = std::nullopt,
        std::optional<float> depth_clear_value = std::nullopt,
        std::optional<uint8_t> stencil_clear_value = std::nullopt) override;

    void ClearTextureFloat(graphics::Texture* _t, TextureSubResourceSet sub_resources, const Color& clearColor) override;
    void CopyBuffer(
        graphics::Buffer& dst, size_t dst_offset,
        const graphics::Buffer& src, size_t src_offset,
        size_t size) override;

    //! Binds the provided shader-visible descriptor heaps to the underlying
    //! D3D12 command list.
    void SetupDescriptorTables(std::span<detail::ShaderVisibleHeapInfo> heaps);

    // D3D12 specific commands
    //! Clears a D3D12 Depth Stencil View.
    OXYGEN_D3D12_API void ClearDepthStencilView(D3D12_CPU_DESCRIPTOR_HANDLE dsv_handle, D3D12_CLEAR_FLAGS clear_flags, float depth, uint8_t stencil);
    //! Sets D3D12 Render Targets.
    OXYGEN_D3D12_API void SetRenderTargets(UINT num_render_target_descriptors, const D3D12_CPU_DESCRIPTOR_HANDLE* rtv_handles, bool rts_single_handle_to_descriptor_range, const D3D12_CPU_DESCRIPTOR_HANDLE* dsv_handle);
    //! Sets the D3D12 Input Assembler primitive topology.
    OXYGEN_D3D12_API void IASetPrimitiveTopology(D3D12_PRIMITIVE_TOPOLOGY topology);
    // Add other D3D12 specific methods as needed, e.g., for root arguments:
    // OXYGEN_D3D12_API void SetGraphicsRootConstantBufferView(UINT root_parameter_index, D3D12_GPU_VIRTUAL_ADDRESS buffer_location);

protected:
    void ExecuteBarriers(std::span<const graphics::detail::Barrier> barriers) override;

private:
    [[nodiscard]] auto GetConcreteCommandList() const -> CommandList*;

    void ResetState();

    Renderer* renderer_;

    size_t graphics_pipeline_hash_ = 0;
    size_t compute_pipeline_hash_ = 0;
};

} // namespace oxygen::graphics::d3d12
