//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <memory>

#include <glm/vec4.hpp>

#include <Oxygen/Base/Macros.h>
#include <Oxygen/Graphics/Common/api_export.h>

namespace oxygen::graphics {

class CommandList;
class CommandQueue;
class IShaderByteCode;
class Buffer;
class RenderTarget;

enum ClearFlags : uint8_t {
    kClearFlagsColor = (1 << 0),
    kClearFlagsDepth = (1 << 1),
    kClearFlagsStencil = (1 << 2),
};

class CommandRecorder {
public:
    OXYGEN_GFX_API CommandRecorder(CommandList* command_list, CommandQueue* target_queue);

    OXYGEN_GFX_API virtual ~CommandRecorder() = default;

    OXYGEN_MAKE_NON_COPYABLE(CommandRecorder)
    OXYGEN_MAKE_NON_MOVABLE(CommandRecorder)

    [[nodiscard]] auto GetTargetQueue() const { return target_queue_; }

    OXYGEN_GFX_API virtual void Begin();
    OXYGEN_GFX_API virtual auto End() -> graphics::CommandList*;

    // Graphics commands
    virtual void Clear(uint32_t flags, uint32_t num_targets, const uint32_t* slots, const glm::vec4* colors, float depth_value, uint8_t stencil_value) = 0;
    virtual void Draw(uint32_t vertex_num, uint32_t instances_num, uint32_t vertex_offset, uint32_t instance_offset) = 0;
    virtual void DrawIndexed(uint32_t index_num, uint32_t instances_num, uint32_t index_offset, int32_t vertex_offset, uint32_t instance_offset) = 0;
    virtual void SetVertexBuffers(uint32_t num, const std::shared_ptr<Buffer>* vertex_buffers, const uint32_t* strides, const uint32_t* offsets) = 0;

    virtual void SetViewport(float left, float width, float top, float height, float min_depth, float max_depth) = 0;
    virtual void SetScissors(int32_t left, int32_t top, int32_t right, int32_t bottom) = 0;
    virtual void SetRenderTarget(std::unique_ptr<RenderTarget> render_target) = 0;
    virtual void SetPipelineState(const std::shared_ptr<IShaderByteCode>& vertex_shader, const std::shared_ptr<IShaderByteCode>& pixel_shader) = 0;

protected:
    [[nodiscard]] auto GetCommandList() const -> CommandList* { return command_list_; }

private:
    CommandList* command_list_;
    CommandQueue* target_queue_;
};

} // namespace oxygen::graphics
