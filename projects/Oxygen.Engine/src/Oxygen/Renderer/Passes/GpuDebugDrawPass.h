//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#if 0
#  include <Oxygen/Renderer/Passes/GraphicsRenderPass.h>
#  include <memory>
#endif

#include <array>
#include <cstddef>
#include <memory>
#include <optional>

#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Base/Types/Geometry.h>
#include <Oxygen/Renderer/Passes/GraphicsRenderPass.h>

#include <Oxygen/Renderer/api_export.h>

namespace oxygen {
class Graphics;
}

namespace oxygen::graphics {
class Buffer;
class Texture;
} // namespace oxygen::graphics

namespace oxygen::engine {

class GpuDebugDrawPass final : public GraphicsRenderPass {
public:
  OXGN_RNDR_API explicit GpuDebugDrawPass(observer_ptr<Graphics> gfx);
  OXGN_RNDR_API ~GpuDebugDrawPass() override;

  OXYGEN_MAKE_NON_COPYABLE(GpuDebugDrawPass)
  OXYGEN_DEFAULT_MOVABLE(GpuDebugDrawPass)

  //! Explicitly set the color texture to render into.
  OXGN_RNDR_API auto SetColorTexture(
    std::shared_ptr<const graphics::Texture> texture) -> void
  {
    color_texture_ = std::move(texture);
  }

  /*!
   Sets the last mouse-down position for GPU debug overlays.

   @param position The last mouse-down position in window coordinates, or
     std::nullopt when no click has been captured.

  ### Performance Characteristics

  - Time Complexity: O(1)
  - Memory: None
  - Optimization: None
  */
  OXGN_RNDR_API auto SetMouseDownPosition(
    std::optional<SubPixelPosition> position) -> void
  {
    mouse_down_position_ = position;
  }

protected:
  //=== GraphicsRenderPass Interface ===--------------------------------------//

  auto ValidateConfig() -> void override;
  auto DoPrepareResources(graphics::CommandRecorder& recorder)
    -> co::Co<> override;
  auto DoExecute(graphics::CommandRecorder& recorder) -> co::Co<> override;

  auto CreatePipelineStateDesc() -> graphics::GraphicsPipelineDesc override;
  auto NeedRebuildPipelineState() const -> bool override;

private:
  auto EnsurePassConstantsBuffer() -> void;
  auto ReleasePassConstantsBuffer() -> void;
  auto UpdatePassConstants() -> void;

  static constexpr uint32_t kPassConstantsStride = 256u;
  static constexpr std::size_t kPassConstantsSlots = 8u;

  std::shared_ptr<const graphics::Texture> color_texture_;
  std::shared_ptr<graphics::Buffer> pass_constants_buffer_ {};
  std::byte* pass_constants_mapped_ptr_ { nullptr };
  std::array<ShaderVisibleIndex, kPassConstantsSlots>
    pass_constants_indices_ {};
  std::size_t pass_constants_slot_ { 0u };
  std::optional<SubPixelPosition> mouse_down_position_ {};
};

} // namespace oxygen::engine
