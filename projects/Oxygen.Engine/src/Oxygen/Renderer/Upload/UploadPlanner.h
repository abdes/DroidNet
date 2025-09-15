//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstdint>
#include <expected>
#include <span>
#include <vector>

#include <Oxygen/Graphics/Common/Buffer.h>
#include <Oxygen/Graphics/Common/Texture.h>
#include <Oxygen/Renderer/Upload/Types.h>
#include <Oxygen/Renderer/Upload/UploadPolicy.h>
#include <Oxygen/Renderer/api_export.h>

// TODO: move to graphics module later

namespace oxygen::graphics {

//! Minimal, shared buffer region descriptor (no destination handle).
/*! Describes a byte range copy from a single staging buffer into a
    destination buffer. The destination buffer is resolved externally.

  ### Invariants

  - Offsets and size are in bytes.
  - src_offset is assigned to respect alignment policy used by the planner.
  - size > 0 for valid uploads.
*/
struct BufferUploadRegion {
  std::uint64_t dst_offset { 0 }; //!< Destination byte offset
  std::uint64_t src_offset { 0 }; //!< Source byte offset in the staging buffer
  std::uint64_t size { 0 }; //!< Number of bytes to copy
};

} // namespace oxygen::graphics

namespace oxygen::engine::upload {

//! Plans copy regions and staging footprints for uploads.
struct TextureUploadPlan {
  uint64_t total_bytes { 0 };
  std::vector<oxygen::graphics::TextureUploadRegion> regions;
};

//! Upload item mapping a region to the original requests it covers.
/*! In the initial (non-coalesced) plan, each item covers exactly one request
    (request_indices.size() == 1). After optimization, items may cover
    multiple requests; indices refer to the input span passed to PlanBuffers.

  ### Invariants

  - region.size > 0
  - request_indices is non-empty
*/
struct UploadItem {
  oxygen::graphics::BufferUploadRegion region;
  std::vector<std::size_t> request_indices;
};

//! Plans copy regions and staging footprint for buffer uploads.
/*! Regions are sorted by destination identity (via request_indices[0]) and
    then by dst_offset. src_offset is aligned according to policy.

  ### Invariants

  - total_bytes is the minimal size required to hold all regions given
    assigned src_offset and sizes.
  - uploads is empty iff total_bytes == 0.
*/
struct BufferUploadPlan {
  std::vector<UploadItem> uploads;
  uint64_t total_bytes { 0 };
};

//! Compute buffer and texture footprints and regions from requests.
class UploadPlanner {
public:
  //=== Buffer planning ------------------------------------------------------//

  //! Stage 1: Plan a batch of buffer uploads into a single staging allocation.
  /*! Packs valid requests, assigns aligned src_offset, no coalescing.
   Invalid requests (null dst, size==0, out-of-bounds) are skipped.
  */
  OXGN_RNDR_API static auto PlanBuffers(std::span<const UploadRequest> requests,
    const UploadPolicy& policy) -> std::expected<BufferUploadPlan, UploadError>;

  //! Stage 3: Coalesce contiguous regions targeting the same destination.
  /*! Produces a new plan with fewer uploads by merging adjacent items when
   both dst_offset and src_offset are contiguous. request_indices are
   concatenated preserving order. The representative request for a merged
   item is request_indices.front().
  */
  OXGN_RNDR_API static auto OptimizeBuffers(
    std::span<const UploadRequest> requests, const BufferUploadPlan& plan,
    const UploadPolicy& policy) -> std::expected<BufferUploadPlan, UploadError>;

  //=== Texture planning -----------------------------------------------------//

  //! Plan a Texture2D upload based on the request description and subresources.
  /*!
   Uses policy-driven alignment rules. If subresources is empty, plans a full
   texture upload for mip 0, array slice 0. When a destination texture is
   present in desc.dst, its descriptor overrides width/height/format.
  */
  OXGN_RNDR_API static auto PlanTexture2D(const UploadTextureDesc& desc,
    std::span<const UploadSubresource> subresources, const UploadPolicy& policy)
    -> std::expected<TextureUploadPlan, UploadError>;

  //! Plan a Texture3D upload across depth slices.
  /*! Plans row/slice pitches per 2D slice; total bytes account for depth.
   If subresources is empty, plans a full upload for mip 0, depth=full,
   array_slice=0. Partial regions use x/y/z/width/height/depth where depth=0
   means full depth at that mip.
  */
  OXGN_RNDR_API static auto PlanTexture3D(const UploadTextureDesc& desc,
    std::span<const UploadSubresource> subresources, const UploadPolicy& policy)
    -> std::expected<TextureUploadPlan, UploadError>;
};

} // namespace oxygen::engine::upload
