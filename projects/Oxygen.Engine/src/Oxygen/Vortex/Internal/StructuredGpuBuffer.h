//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstdint>
#include <memory>
#include <string>

#include <Oxygen/Base/Macros.h>
#include <Oxygen/Core/Bindless/Types.h>
#include <Oxygen/Graphics/Common/Buffer.h>

namespace oxygen {
class Graphics;
} // namespace oxygen

namespace oxygen::vortex::internal {

//! A device-local structured buffer with shader-visible SRV and UAV.
/*!
 `Ensure` grows the buffer to hold at least the requested element count. Growth
 replaces the buffer without copying its contents, and retires the old buffer
 after the GPU is done with it. The destructor retires the buffer the same way.
*/
class StructuredGpuBuffer {
public:
  StructuredGpuBuffer(std::string debug_name, std::uint32_t stride,
    graphics::BufferUsage extra_usage = graphics::BufferUsage::kNone);
  ~StructuredGpuBuffer();

  OXYGEN_MAKE_NON_COPYABLE(StructuredGpuBuffer)
  OXYGEN_MAKE_NON_MOVABLE(StructuredGpuBuffer)

  //! Grows the buffer to at least `element_count` elements; false on failure.
  auto Ensure(const std::shared_ptr<Graphics>& gfx, std::uint32_t element_count)
    -> bool;

  [[nodiscard]] auto GetBuffer() const noexcept -> graphics::Buffer*
  {
    return buffer_.get();
  }
  [[nodiscard]] auto GetSrv() const noexcept -> ShaderVisibleIndex
  {
    return srv_;
  }
  [[nodiscard]] auto GetUav() const noexcept -> ShaderVisibleIndex
  {
    return uav_;
  }
  [[nodiscard]] auto GetCapacity() const noexcept -> std::uint32_t
  {
    return capacity_;
  }

private:
  auto Retire() -> void;

  std::string debug_name_;
  std::uint32_t stride_;
  graphics::BufferUsage usage_;
  std::weak_ptr<Graphics> gfx_;
  std::shared_ptr<graphics::Buffer> buffer_;
  ShaderVisibleIndex srv_ { kInvalidShaderVisibleIndex };
  ShaderVisibleIndex uav_ { kInvalidShaderVisibleIndex };
  std::uint32_t capacity_ { 0U };
};

} // namespace oxygen::vortex::internal
