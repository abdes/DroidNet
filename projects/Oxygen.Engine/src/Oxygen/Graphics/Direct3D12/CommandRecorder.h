//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include "Oxygen/Graphics/Common/CommandRecorder.h"
#include "Oxygen/Graphics/Direct3d12/CommandList.h"

#include <wrl/client.h>

namespace oxygen::graphics::d3d12 {

class RenderTarget;

class CommandRecorder final : public graphics::CommandRecorder {
    using Base = graphics::CommandRecorder;

public:
    explicit CommandRecorder(const CommandListType type)
        : Base(type)
    {
    }
    ~CommandRecorder() override = default;

    OXYGEN_MAKE_NON_COPYABLE(CommandRecorder);
    OXYGEN_MAKE_NON_MOVABLE(CommandRecorder);

    void Begin() override;
    auto End() -> CommandListPtr override;

    // TODO: push up to base class
    void SetViewport(float left, float width, float top, float height, float min_depth, float max_depth) override;
    void SetScissors(int32_t left, int32_t top, int32_t right, int32_t bottom) override;
    void SetRenderTarget(RenderTargetNoDeletePtr render_target) override;

    void SetPipelineState(const std::shared_ptr<IShaderByteCode>& vertex_shader, const std::shared_ptr<IShaderByteCode>& pixel_shader) override;
    void SetVertexBuffers(uint32_t num, const BufferPtr* vertex_buffers, const uint32_t* strides, const uint32_t* offsets) override;
    void Clear(uint32_t flags, uint32_t num_targets, const uint32_t* slots, const glm::vec4* colors, float depth_value, uint8_t stencil_value) override;
    void Draw(uint32_t vertex_num, uint32_t instances_num, uint32_t vertex_offset, uint32_t instance_offset) override;
    void DrawIndexed(uint32_t index_num, uint32_t instances_num, uint32_t index_offset, int32_t vertex_offset, uint32_t instance_offset) override;

protected:
    void InitializeCommandRecorder() override;
    void ReleaseCommandRecorder() noexcept override;

private:
    void ResetState();

    void CreateRootSignature();

    std::unique_ptr<CommandList> current_command_list_;
    const RenderTarget* current_render_target_ { nullptr };
    Microsoft::WRL::ComPtr<ID3D12PipelineState> pipeline_state_;
    Microsoft::WRL::ComPtr<ID3D12RootSignature> root_signature_;
};

} // namespace oxygen::graphics::d3d12
