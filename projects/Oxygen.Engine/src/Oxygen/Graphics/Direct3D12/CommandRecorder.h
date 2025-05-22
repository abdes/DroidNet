//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <span>

#include <Oxygen/Graphics/Common/CommandRecorder.h>
#include <Oxygen/Graphics/Common/Types/Color.h>
#include <Oxygen/Graphics/Direct3D12/CommandList.h>

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

class CommandRecorder final : public graphics::CommandRecorder {
    using Base = graphics::CommandRecorder;

public:
    CommandRecorder(graphics::CommandList* command_list, graphics::CommandQueue* target_queue);
    ~CommandRecorder() override = default;

    OXYGEN_MAKE_NON_COPYABLE(CommandRecorder);
    OXYGEN_MAKE_NON_MOVABLE(CommandRecorder);

    void Begin() override;
    auto End() -> graphics::CommandList* override;

    void SetViewport(const ViewPort& viewport) override;
    void SetScissors(const Scissors& scissors) override;

    void SetPipelineState(const std::shared_ptr<IShaderByteCode>& vertex_shader, const std::shared_ptr<IShaderByteCode>& pixel_shader) override;
    void SetVertexBuffers(uint32_t num, const std::shared_ptr<graphics::Buffer>* vertex_buffers, const uint32_t* strides, const uint32_t* offsets) override;
    void Draw(uint32_t vertex_num, uint32_t instances_num, uint32_t vertex_offset, uint32_t instance_offset) override;
    void DrawIndexed(uint32_t index_num, uint32_t instances_num, uint32_t index_offset, int32_t vertex_offset, uint32_t instance_offset) override;

    void BindFrameBuffer(const graphics::Framebuffer& framebuffer) override;
    virtual void ClearFramebuffer(
        const oxygen::graphics::Framebuffer& framebuffer,
        std::optional<std::vector<std::optional<Color>>> color_clear_values = std::nullopt,
        std::optional<float> depth_clear_value = std::nullopt,
        std::optional<uint8_t> stencil_clear_value = std::nullopt) override;

    void ClearTextureFloat(graphics::Texture* _t, TextureSubResourceSet sub_resources, const Color& clearColor) override;

    //! Binds the provided shader-visible descriptor heaps to the underlying
    //! D3D12 command list.
    void SetupDescriptorTables(std::span<detail::ShaderVisibleHeapInfo> heaps);

protected:
    void ExecuteBarriers(std::span<const graphics::detail::Barrier> barriers) override;

private:
    [[nodiscard]] auto GetConcreteCommandList() const -> CommandList*;

    void ResetState();

    void CreateRootSignature();

    Microsoft::WRL::ComPtr<ID3D12PipelineState> pipeline_state_;
    Microsoft::WRL::ComPtr<ID3D12RootSignature> root_signature_;
};

} // namespace oxygen::graphics::d3d12
