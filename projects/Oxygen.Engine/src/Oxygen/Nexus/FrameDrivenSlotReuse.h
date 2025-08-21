//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at <https://opensource.org/licenses/BSD-3-Clause>.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <vector>

#include <Oxygen/Core/Types/BindlessHandle.h>
#include <Oxygen/Core/Types/Frame.h>
#include <Oxygen/Graphics/Common/DescriptorAllocator.h>
#include <Oxygen/Graphics/Common/Detail/PerFrameResourceManager.h>
#include <Oxygen/Nexus/DomainIndexMapper.h>
#include <Oxygen/Nexus/GenerationTracker.h>
#include <Oxygen/Nexus/Types/Domain.h>
#include <Oxygen/Nexus/api_export.h>

namespace oxygen::nexus {

//! Frame-driven deferred reuse strategy for bindless descriptor slots.
/*!
 Manages bindless descriptor slot allocation and reclamation with frame-based
 deferred cleanup and generation tracking for stale handle detection.

 ### Key Features

 - **Deferred Reclamation**: Slots are freed via PerFrameResourceManager to
   avoid GPU synchronization issues
 - **Generation Tracking**: Each slot has a generation counter for CPU-side
   stale handle detection
 - **Thread-Safe**: Uses atomic operations and proper memory ordering for
   concurrent access
 - **Double-Release Protection**: Prevents multiple releases of the same handle

 ### Usage Patterns

 Typical workflow involves allocating handles during resource creation,
 releasing them when resources are destroyed, and checking validity before use.

 ### Architecture Notes

 The strategy separates allocation/release from actual GPU resource management,
 allowing the graphics backend to control timing of descriptor heap operations
 while providing immediate feedback for invalid handles.

 @see VersionedBindlessHandle, PerFrameResourceManager, GenerationTracker
*/
class FrameDrivenSlotReuse {
public:
  //! Type-erased backend allocate function: returns absolute handle index.
  using AllocateFn = std::function<oxygen::bindless::Handle(DomainKey)>;

  //! Type-erased backend free function.
  using FreeFn = std::function<void(DomainKey, oxygen::bindless::Handle)>;

  //! Construct the strategy with backend hooks and per-frame infrastructure.
  OXGN_NXS_API explicit FrameDrivenSlotReuse(AllocateFn allocate, FreeFn free,
    oxygen::graphics::detail::PerFrameResourceManager& per_frame);

  //! Allocate a bindless slot in the specified domain.
  OXGN_NXS_NDAPI auto Allocate(DomainKey domain)
    -> oxygen::VersionedBindlessHandle;

  //! Release a previously allocated handle; reclamation is deferred.
  OXGN_NXS_API void Release(
    DomainKey domain, oxygen::VersionedBindlessHandle h);

  //! Returns true if the handle's generation matches the current slot state.
  OXGN_NXS_NDAPI auto IsHandleCurrent(
    oxygen::VersionedBindlessHandle h) const noexcept -> bool;

  //! Forward the frame-begin event to the per-frame buckets.
  OXGN_NXS_API void OnBeginFrame(oxygen::frame::Slot fi);

private:
  //! Ensure internal buffers have capacity for the provided bindless index.
  void EnsureCapacity_(oxygen::bindless::Handle idx);

  AllocateFn allocate_;
  FreeFn free_;
  oxygen::graphics::detail::PerFrameResourceManager& per_frame_;

  // Generation tracking for stale-handle detection.
  mutable GenerationTracker generations_;

  // Double-release guard: 0 = not pending, 1 = pending free.
  //
  // Use a contiguous `std::atomic<uint8_t>[]` (owned by a unique_ptr) instead
  // of `std::vector<std::atomic<uint8_t>>` to ensure a predictable contiguous
  // layout and explicit pointer-stability during resizes. Some standard-library
  // implementations do not treat `std::atomic` as trivially relocatable, which
  // makes vector reallocation semantics platform-dependent. The code relies on
  // holding `resize_mutex_` briefly to protect pointer stability for deferred
  // reclamation lambdas that access these flags.
  std::unique_ptr<std::atomic<uint8_t>[]> pending_flags_;
  std::size_t pending_size_ { 0 };
  mutable std::mutex resize_mutex_;
};

} // namespace oxygen::nexus
