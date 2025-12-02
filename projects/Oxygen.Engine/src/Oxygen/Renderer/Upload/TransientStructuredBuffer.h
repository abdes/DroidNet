//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstdint>
#include <expected>
#include <optional>

#include <Oxygen/Base/Macros.h>
#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Core/Bindless/Types.h>
#include <Oxygen/Graphics/Common/Graphics.h>
#include <Oxygen/Renderer/Upload/StagingProvider.h>
#include <Oxygen/Renderer/api_export.h>

namespace oxygen::engine::upload {

//! Wrapper around StagingProvider for per-frame transient structured buffers.
/*!
 Wrapper around StagingProvider (e.g. RingBuffer) for per-frame transient
 structured buffers.

 ### Design Philosophy
 This class implements a "Direct Write" strategy for dynamic data, as opposed to
 the "Staging + Copy" strategy used by `AtlasBuffer`.

 - **Mechanism**: Allocates CPU-visible memory (Upload Heap) that is directly
   readable by the GPU over PCIe.
 - **Usage**: Call `Allocate()`, write data via `GetMappedPtr()`, and bind.
 - **Performance**:
   - **Pros**: Zero copy overhead (no CopyBufferRegion commands), low latency,
     simple synchronization (N-buffering handled by RingBuffer).
   - **Cons**: Slower GPU read access (PCIe vs VRAM).
 - **Best For**: High-frequency data updated every frame (Transforms, Particles,
   Draw Commands) where the cost of managing copies outweighs the read latency.

 ### Transparent Features from RingBufferStaging
 By wrapping `RingBufferStaging`, this class inherits critical features without
 implementing them:
 - **N-Buffering**: The underlying ring buffer automatically cycles through
   memory partitions (Frame 0, Frame 1, ...), ensuring we never write to memory
   the GPU is currently reading.
 - **Synchronization**: `RingBufferStaging` handles fence tracking. If the ring
   wraps around to a partition that is still in use, it waits or grows
   automatically.
 - **Memory Management**: No manual deallocation is needed. The memory is
   "released" by simply advancing the ring head.

 Usage:
 1. Call Allocate(count) at start of frame/view.
 2. Write data to GetMappedPtr().
 3. Use GetBinding() to bind to shader.
 4. Call Reset() at end of frame (or let destructor handle it).
*/
class TransientStructuredBuffer {
public:
  struct Binding {
    ShaderVisibleIndex srv { kInvalidShaderVisibleIndex };
    std::uint32_t stride { 0 };
  };

  OXGN_RNDR_API TransientStructuredBuffer(
    observer_ptr<Graphics> gfx, StagingProvider& staging, std::uint32_t stride);

  OXYGEN_MAKE_NON_COPYABLE(TransientStructuredBuffer)
  OXYGEN_DEFAULT_MOVABLE(TransientStructuredBuffer)

  OXGN_RNDR_API ~TransientStructuredBuffer();

  //! Allocate memory and create a transient SRV.
  OXGN_RNDR_API auto Allocate(std::uint32_t element_count)
    -> std::expected<void, std::error_code>;

  //! Get the current binding (SRV + Stride).
  OXGN_RNDR_NDAPI auto GetBinding() const noexcept -> Binding
  {
    return { srv_index_, stride_ };
  }

  //! Get pointer to mapped memory for writing.
  OXGN_RNDR_NDAPI auto GetMappedPtr() const noexcept -> void*
  {
    return allocation_.has_value() ? allocation_->Ptr() : nullptr;
  }

  //! Release the SRV descriptor. Memory is recycled by StagingProvider
  //! automatically.
  OXGN_RNDR_API auto Reset() -> void;

private:
  observer_ptr<Graphics> gfx_;
  StagingProvider* staging_; // stored as pointer for assignability
  std::uint32_t stride_;

  std::optional<StagingProvider::Allocation> allocation_;
  ShaderVisibleIndex srv_index_ { kInvalidShaderVisibleIndex };
  oxygen::graphics::NativeView native_view_ {};
};

} // namespace oxygen::engine::upload
