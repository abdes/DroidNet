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
#include <Oxygen/Core/Types/BindlessHandle.h>
#include <Oxygen/Core/Types/Frame.h>
#include <Oxygen/Graphics/Common/Buffer.h>
#include <Oxygen/Graphics/Common/Graphics.h>
#include <Oxygen/Graphics/Common/ResourceRegistry.h>
#include <Oxygen/Renderer/Resources/UploadHelpers.h>
#include <Oxygen/Renderer/Upload/Types.h>
#include <Oxygen/Renderer/api_export.h>

namespace oxygen::renderer::resources {

//=== AtlasBuffer -----------------------------------------------------------//

//! Simple atlas owner for DEFAULT structured buffers with a stable SRV
//! (primary) and optional overflow chunks. Phase 1 uses only the primary;
//! API remains ready for multi-chunk hybrid growth.
// Forward-declare an internal tag type used to gate ElementRef construction.
// The definition lives in the .cpp to prevent external instantiation.
struct ElementRefTag;

class AtlasBuffer {
public:
  struct Stats {
    std::uint64_t ensure_calls { 0 };
    std::uint64_t allocations { 0 };
    std::uint64_t releases { 0 };
    std::uint32_t capacity_elements { 0 };
    std::uint32_t next_index { 0 };
    std::uint32_t free_list_size { 0 };
  };
  class ElementRef {
  public:
    // Trivially copyable/movable; invalid by default. Construction with
    // values is restricted to AtlasBuffer via a private, tagged ctor.
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

  enum class EnsureResult {
    kUnchanged,
    kCreated,
    kResized,
  };

  OXGN_RNDR_API AtlasBuffer(
    observer_ptr<Graphics> gfx, std::uint32_t stride, std::string debug_label);

  OXYGEN_MAKE_NON_COPYABLE(AtlasBuffer)
  OXYGEN_MAKE_NON_MOVABLE(AtlasBuffer)

  OXGN_RNDR_API ~AtlasBuffer();

  //! Ensure capacity for at least min_elements in the primary buffer.
  //! Phase 1: We only grow the primary via EnsureBufferAndSrv.
  OXGN_RNDR_API auto EnsureCapacity(std::uint32_t min_elements, float slack)
    -> std::expected<EnsureResult, std::error_code>;

  //! Allocate one element and return an ElementRef on success.
  //! Phase 1: Allocates from primary only. Returns errors on invalid request
  //! or when capacity is insufficient (caller should EnsureCapacity first).
  OXGN_RNDR_API auto Allocate(std::uint32_t count = 1)
    -> std::expected<ElementRef, std::error_code>;

  //! Release an element reference; retires on given frame slot.
  OXGN_RNDR_API auto Release(ElementRef ref, frame::Slot slot) -> void;

  //! Recycle elements retired in this frame slot back to the free list.
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

  //! Build a buffer-upload descriptor for an element reference.
  //! Validates that the ref targets a known chunk and the element is in
  //! range. Returns a fully-populated UploadBufferDesc on success.
  OXGN_RNDR_API auto MakeUploadDesc(
    const ElementRef& ref, std::uint64_t size_bytes) const
    -> std::expected<oxygen::engine::upload::UploadBufferDesc, std::error_code>;

  //! Build a buffer-upload descriptor for a specific element index.
  //! This bypasses ElementRef and directly targets element_index within the
  //! primary buffer. Returns error if buffer is not available or index is out
  //! of range.
  OXGN_RNDR_API auto MakeUploadDescForIndex(
    std::uint32_t element_index, std::uint64_t size_bytes) const
    -> std::expected<oxygen::engine::upload::UploadBufferDesc, std::error_code>;

  //! Lightweight binding description for Phase 1 (single chunk).
  struct Binding {
    ShaderVisibleIndex srv { kInvalidShaderVisibleIndex };
    std::uint32_t stride { 0 };
  };

  //! Get current binding info (srv, stride). The SRV becomes valid after
  //! EnsureCapacity() creates the buffer.
  OXGN_RNDR_NDAPI auto GetBinding() const noexcept -> Binding
  {
    return Binding { primary_srv_, stride_ };
  }

  //! Read-only helpers to inspect an ElementRef without exposing internals.
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

} // namespace oxygen::renderer::resources
