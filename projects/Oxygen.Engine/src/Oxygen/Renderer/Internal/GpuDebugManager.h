//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstdint>
#include <memory>

#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Core/Bindless/Types.h>
#include <Oxygen/Renderer/api_export.h>

namespace oxygen {
class Graphics;
namespace graphics {
  class Buffer;
  class CommandRecorder;
}
} // namespace oxygen::graphics

namespace oxygen::engine::internal {

//! Manages GPU debug resources (line buffer and counters) for bindless
//! debugging.
class GpuDebugManager {
public:
  OXGN_RNDR_API explicit GpuDebugManager(observer_ptr<Graphics> gfx);
  OXGN_RNDR_API ~GpuDebugManager();

  OXYGEN_MAKE_NON_COPYABLE(GpuDebugManager)
  OXYGEN_DEFAULT_MOVABLE(GpuDebugManager)

  //! Prepare resources for the current frame.
  //! Traditionally resets counters using a compute shader or clear operation.
  OXGN_RNDR_API auto OnFrameStart(graphics::CommandRecorder& recorder) -> void;

  [[nodiscard]] auto GetLineBufferSrvIndex() const noexcept -> uint32_t
  {
    return line_buffer_srv_.get();
  }

  [[nodiscard]] auto GetLineBufferUavIndex() const noexcept -> uint32_t
  {
    return line_buffer_uav_.get();
  }

  [[nodiscard]] auto GetCounterBufferUavIndex() const noexcept -> uint32_t
  {
    return counter_buffer_uav_.get();
  }

  [[nodiscard]] auto GetLineBuffer() const noexcept
    -> const std::shared_ptr<graphics::Buffer>&
  {
    return line_buffer_;
  }

  [[nodiscard]] auto GetCounterBuffer() const noexcept
    -> const std::shared_ptr<graphics::Buffer>&
  {
    return counter_buffer_;
  }

private:
  observer_ptr<Graphics> gfx_;
  std::shared_ptr<graphics::Buffer> line_buffer_;
  std::shared_ptr<graphics::Buffer> counter_buffer_;

  ShaderVisibleIndex line_buffer_srv_ { kInvalidShaderVisibleIndex };
  ShaderVisibleIndex line_buffer_uav_ { kInvalidShaderVisibleIndex };
  ShaderVisibleIndex counter_buffer_uav_ { kInvalidShaderVisibleIndex };
};

} // namespace oxygen::engine::internal
