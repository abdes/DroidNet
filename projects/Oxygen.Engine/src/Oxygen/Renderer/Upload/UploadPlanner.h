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

namespace oxygen::engine::upload {

//! Plans copy regions and staging footprints for uploads.
struct TextureUploadPlan {
  uint64_t total_bytes { 0 };
  std::vector<oxygen::graphics::TextureUploadRegion> regions;
};

//! Plans copy regions and staging footprints for uploads.
struct BufferUploadPlan {
  struct CopyRegion {
    std::shared_ptr<oxygen::graphics::Buffer> dst;
    uint64_t dst_offset { 0 };
    uint64_t size { 0 };
    uint64_t src_offset { 0 }; // offset within staging buffer
    size_t request_index { 0 }; // index in the input batch/span
  };
  uint64_t total_bytes { 0 };
  std::vector<CopyRegion> regions;
};

//! Compute buffer and texture footprints and regions from requests.
class UploadPlanner {
public:
  //=== Buffer planning ------------------------------------------------------//

  //! Plan a batch of buffer uploads into a single staging allocation.
  /*! Packs valid buffer requests into a contiguous staging buffer abiding by
   policy alignments. Returns per-request staging offsets for CopyBuffer.
   Invalid requests (null dst or size==0) are skipped.
  */
  OXGN_RNDR_API static auto PlanBuffers(std::span<const UploadRequest> requests,
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
