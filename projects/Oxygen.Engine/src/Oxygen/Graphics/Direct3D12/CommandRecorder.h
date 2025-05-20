//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <Oxygen/Graphics/Common/CommandRecorder.h>
#include <Oxygen/Graphics/Direct3D12/CommandList.h>

#include <wrl/client.h>

namespace oxygen::graphics::d3d12 {

class RenderTarget;

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

    void ClearTextureFloat(graphics::Texture* _t, TextureSubResourceSet sub_resources, const Color& clearColor) override;

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
