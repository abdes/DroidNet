//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstdint>
#include <deque>
#include <expected>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <Oxygen/Base/Macros.h>
#include <Oxygen/Core/Bindless/Types.h>
#include <Oxygen/Core/Types/Frame.h>
#include <Oxygen/Graphics/Common/Buffer.h>
#include <Oxygen/Graphics/Common/Graphics.h>
#include <Oxygen/Graphics/Common/ResourceRegistry.h>
#include <Oxygen/Renderer/Upload/Types.h>
#include <Oxygen/Renderer/Upload/UploadHelpers.h>
#include <Oxygen/Renderer/api_export.h>

namespace oxygen::engine::upload {

// Forward-declare an internal tag type used to gate ElementRef construction.
// The definition lives in the .cpp to prevent external instantiation.
struct ElementRefTag;

//! DEFAULT structured-buffer atlas with a stable primary SRV.
/*!
 Simple owner that manages element-based suballocation inside a single DEFAULT
 (device-local) structured buffer (the *primary* chunk) during Phase 1. The
 public API is intentionally forward-looking: support for overflow / multi-chunk
 growth can be layered on later without changing existing call sites.

 ### Key Features

 - **Stable SRV**: A single shader-visible SRV remains stable across growth
   operations (resize triggers re-creation but preserves the SRV index
   abstraction).
 - **Element Allocation**: Provides fixed-size element slots addressed by index;
   only single-element Allocate() is supported in Phase 1.
 - **Frame-Deferred Recycle**: Freed elements enter a retire list keyed by
   frame::Slot and are recycled when OnFrameStart(slot) is invoked for that
   slot, preventing use-after-free hazards while GPU work may still reference
   previous frames.
 - **Descriptor Construction**: Helpers build UploadBufferDesc instances for
   either an ElementRef or a raw element index.

 ### Usage Patterns

 1. Call EnsureCapacity(min, slack) before allocating to grow/create the
    underlying buffer. (Phase 1 does not auto-grow during Allocate()).
 2. Allocate() returns an ElementRef which is later passed to Release().
 3. Call OnFrameStart(current_slot) each frame to recycle retired elements for
    that slot.
 4. Use MakeUploadDesc() or MakeUploadDescForIndex() to stage CPU->GPU uploads
    for individual elements.

 ### Architecture Notes

 - Growth uses an external helper (EnsureBufferAndSrv) which re-creates the
   Buffer + SRV as needed. Live data migration is intentionally NOT performed in
   Phase 1; callers are responsible for re-uploading.
 - Free list recycling is order-agnostic; tests must not assume LIFO.
 - Multi-count allocation (count > 1) returns std::errc::invalid_argument.

 @warning Phase 1 design intentionally omits overflow chunk support and does not
          migrate or compact existing data during resize.
 @see AtlasBuffer::EnsureCapacity, AtlasBuffer::Allocate,
      AtlasBuffer::MakeUploadDesc, AtlasBuffer::OnFrameStart
*/
class AtlasBuffer {
public:
  //! Lightweight runtime statistics for introspection and testing.
  /*!
   Collected opportunistically; values are updated on key API calls.

   - ensure_calls: Number of EnsureCapacity invocations.
   - allocations / releases: Counts of successful logical operations.
   - capacity_elements: Current element capacity (primary chunk only).
   - next_index: First unallocated sequential index (excludes free list).
   - free_list_size: Current number of recyclable element indices.
  */
  struct Stats {
    std::uint64_t ensure_calls { 0 };
    std::uint64_t allocations { 0 };
    std::uint64_t releases { 0 };
    std::uint32_t capacity_elements { 0 };
    std::uint32_t next_index { 0 };
    std::uint32_t free_list_size { 0 };
  };

  //! Trivially copyable handle referencing an allocated element.
  /*!
   Acts as an opaque token passed back to Release() and descriptor helpers.
   Default constructed references are invalid and rejected by MakeUploadDesc().
  */
  class ElementRef {
  public:
    ElementRef() = default;
    ElementRef(const ElementRef&) = default;
    ElementRef(ElementRef&&) = default;
    auto operator=(const ElementRef&) -> ElementRef& = default;
    auto operator=(ElementRef&&) -> ElementRef& = default;

  private:
    friend class AtlasBuffer; // allow AtlasBuffer to build valid refs
    // Private tagged constructor; tag is defined only in .cpp
    ElementRef(const ElementRefTag&, ShaderVisibleIndex s, std::uint32_t e)
      : srv_(s)
      , element_(e)
    {
    }

    ShaderVisibleIndex srv_ { kInvalidShaderVisibleIndex }; // chunk SRV
    std::uint32_t element_ { 0 }; // element index within the chunk
  };

  //! Construct an AtlasBuffer.
  OXGN_RNDR_API AtlasBuffer(
    observer_ptr<Graphics> gfx, std::uint32_t stride, std::string debug_label);

  OXYGEN_MAKE_NON_COPYABLE(AtlasBuffer)
  OXYGEN_MAKE_NON_MOVABLE(AtlasBuffer)

  OXGN_RNDR_API ~AtlasBuffer();

  //! Ensure minimum element capacity (create/resize if needed).
  OXGN_RNDR_API auto EnsureCapacity(std::uint32_t min_elements, float slack)
    -> std::expected<EnsureBufferResult, std::error_code>;

  //! Allocate a single element slot.
  OXGN_RNDR_API auto Allocate(std::uint32_t count = 1)
    -> std::expected<ElementRef, std::error_code>;

  //! Release an element (deferred recycle).
  OXGN_RNDR_API auto Release(ElementRef ref, frame::Slot slot) -> void;

  //! Recycle retired elements for a frame slot.
  OXGN_RNDR_API auto OnFrameStart(frame::Slot slot) -> void;

  // Accessors and upload helpers
  OXGN_RNDR_NDAPI auto Stride() const noexcept -> std::uint32_t
  {
    return stride_;
  }
  OXGN_RNDR_NDAPI auto CapacityElements() const noexcept -> std::uint32_t
  {
    return capacity_elements_;
  }

  //! Build upload descriptor from ElementRef.
  OXGN_RNDR_API auto MakeUploadDesc(
    const ElementRef& ref, std::uint64_t size_bytes) const
    -> std::expected<oxygen::engine::upload::UploadBufferDesc, std::error_code>;

  //! Build upload descriptor from raw element index.
  OXGN_RNDR_API auto MakeUploadDescForIndex(
    std::uint32_t element_index, std::uint64_t size_bytes) const
    -> std::expected<oxygen::engine::upload::UploadBufferDesc, std::error_code>;

  //! Lightweight binding description for Phase 1 (single chunk).
  struct Binding {
    ShaderVisibleIndex srv { kInvalidShaderVisibleIndex };
    std::uint32_t stride { 0 };
  };

  //! Get current binding info (SRV + stride).
  /*!
   Returns the current binding info (SRV + stride). SRV is invalid until the
   successful EnsureCapacity().

   @return Binding value (by copy).
  */
  OXGN_RNDR_NDAPI auto GetBinding() const noexcept -> Binding
  {
    return Binding { primary_srv_, stride_ };
  }

  //! Read-only helpers for ElementRef inspection.
  /*!
   Provide accessors for retrieving the element index and SRV backing an
   ElementRef. These helpers avoid exposing the internal layout of ElementRef
   while enabling tests and clients to query indices.
  */
  OXGN_RNDR_NDAPI auto GetElementIndex(const ElementRef& ref) const noexcept
    -> std::uint32_t
  {
    return ref.element_;
  }
  OXGN_RNDR_NDAPI auto GetSrvIndex(const ElementRef& ref) const noexcept
    -> ShaderVisibleIndex
  {
    return ref.srv_;
  }

  OXGN_RNDR_NDAPI auto GetStats() const noexcept -> Stats { return stats_; }

private:
  observer_ptr<Graphics> gfx_;
  std::string debug_label_;
  std::uint32_t stride_ { 0 };

  // Primary chunk (Phase 1)
  std::shared_ptr<graphics::Buffer> primary_buffer_;
  ShaderVisibleIndex primary_srv_ {};
  std::uint32_t capacity_elements_ { 0 };
  std::uint32_t next_index_ { 0 };

  // Simple free/retire using indices (Phase 1)
  std::vector<std::uint32_t> free_list_;
  std::array<std::vector<std::uint32_t>, frame::kFramesInFlight> retire_lists_;

  Stats stats_ {};
};

} // namespace oxygen::engine::upload
