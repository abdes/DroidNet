//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include "glm/vec4.hpp"

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Base/Macros.h>
#include <Oxygen/Graphics/Common/Forward.h>
#include <Oxygen/Graphics/Common/Types/CommandListType.h>

namespace oxygen::graphics {

enum ClearFlags : uint8_t {
    kClearFlagsColor = (1 << 0),
    kClearFlagsDepth = (1 << 1),
    kClearFlagsStencil = (1 << 2),
};

class CommandRecorder {
public:
    constexpr explicit CommandRecorder(const CommandListType type)
        : type_ { type }
    {
    }

    virtual ~CommandRecorder() = default;

    OXYGEN_MAKE_NON_COPYABLE(CommandRecorder);
    OXYGEN_MAKE_NON_MOVABLE(CommandRecorder);

    [[nodiscard]] virtual auto GetQueueType() const -> CommandListType { return type_; }

    virtual void Begin() = 0;
    virtual auto End() -> CommandListPtr = 0;

    // Graphics commands
    virtual void Clear(uint32_t flags, uint32_t num_targets, const uint32_t* slots, const glm::vec4* colors, float depth_value, uint8_t stencil_value) = 0;
    virtual void Draw(uint32_t vertex_num, uint32_t instances_num, uint32_t vertex_offset, uint32_t instance_offset) = 0;
    virtual void DrawIndexed(uint32_t index_num, uint32_t instances_num, uint32_t index_offset, int32_t vertex_offset, uint32_t instance_offset) = 0;
    virtual void SetVertexBuffers(uint32_t num, const BufferPtr* vertex_buffers, const uint32_t* strides, const uint32_t* offsets) = 0;

    virtual void SetViewport(float left, float width, float top, float height, float min_depth, float max_depth) = 0;
    virtual void SetScissors(int32_t left, int32_t top, int32_t right, int32_t bottom) = 0;
    virtual void SetRenderTarget(RenderTargetNoDeletePtr render_target) = 0;
    virtual void SetPipelineState(const std::shared_ptr<IShaderByteCode>& vertex_shader, const std::shared_ptr<IShaderByteCode>& pixel_shader) = 0;

protected:
    virtual void InitializeCommandRecorder() = 0;
    virtual void ReleaseCommandRecorder() noexcept = 0;

private:
    CommandListType type_ { CommandListType::kNone };
};

} // namespace oxygen::graphics
