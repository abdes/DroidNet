//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstddef>
#include <cstdint>

#include <Oxygen/Renderer/Upload/Types.h>

namespace oxygen::engine::upload {

struct UploadPolicy {
  struct Batching {
    static constexpr uint32_t kMaxRegionsPerCommandList = 256U;
    static constexpr uint64_t kMaxBytesPerCommandList
      = 64ULL * 1024ULL * 1024ULL; // 64 MB
  };
  struct AlignmentPolicy {
    // D3D12_TEXTURE_DATA_PITCH_ALIGNMENT / Vulkan row pitch alignment (bytes)
    static constexpr Alignment kRowPitchAlignment { 256U };
    // D3D12_TEXTURE_DATA_PLACEMENT_ALIGNMENT (bytes)
    static constexpr Alignment kPlacementAlignment { 512U };
    // Generic buffer copy alignment (bytes)
    static constexpr Alignment kBufferCopyAlignment { 256U };
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
};

OXGN_RNDR_API auto DefaultUploadPolicy() -> UploadPolicy;

} // namespace oxygen::engine::upload
