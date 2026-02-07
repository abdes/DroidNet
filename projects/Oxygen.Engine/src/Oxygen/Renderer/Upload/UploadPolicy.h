//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstddef>
#include <cstdint>

#include <Oxygen/Graphics/Common/Queues.h>
#include <Oxygen/Renderer/Upload/Types.h>
#include <Oxygen/Renderer/api_export.h>

namespace oxygen::engine::upload {

struct UploadPolicy {
  struct Batching {
    static constexpr uint32_t kMaxRegionsPerCommandList = 256U;
    static constexpr uint64_t kMaxBytesPerCommandList
      = 64ULL * 1024ULL * 1024ULL; // 64 MB
  };
  struct AlignmentPolicy {
    // D3D12_TEXTURE_DATA_PITCH_ALIGNMENT / typical Vulkan row pitch (bytes)
    Alignment row_pitch_alignment { 256U };
    // D3D12_TEXTURE_DATA_PLACEMENT_ALIGNMENT (bytes)
    Alignment placement_alignment { 512U };
    // Relaxed buffer copy alignment, although NVIDIA recommends 16 (bytes) for
    // best performance.
    Alignment buffer_copy_alignment { 4U };
  };
  struct Limits {
    static constexpr uint64_t kSmallArenaBlockMin = 64ULL * 1024ULL; // 64 KB
    static constexpr uint64_t kSmallArenaBlockMax = 1024ULL * 1024ULL; // 1 MB
  };
  struct Timeouts {
    static constexpr uint32_t kFlushTimeSliceMs = 2U;
  };
  struct FillerPolicy {
    // When enabled, missing/short producers are padded with this value.
    bool enable_default_fill = true;
    std::byte filler_value { std::byte { 0 } };
  } filler;

  AlignmentPolicy alignment;
  // Queue key to use for upload command recording/signaling. This value is
  // required and must be provided by the caller (for example Renderer via
  // RendererConfig). Do not default-initialize this field.
  oxygen::graphics::QueueKey upload_queue_key;

  // Construct an UploadPolicy with a required upload queue key.
  OXGN_RNDR_API explicit UploadPolicy(oxygen::graphics::QueueKey qkey) noexcept;
  OXGN_RNDR_API UploadPolicy(
    oxygen::graphics::QueueKey qkey, AlignmentPolicy alignment_policy) noexcept;
  UploadPolicy() = delete;
};

OXGN_RNDR_API auto DefaultUploadPolicy() -> UploadPolicy;

} // namespace oxygen::engine::upload
