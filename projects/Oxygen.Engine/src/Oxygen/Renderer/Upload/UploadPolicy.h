//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstdint>

#include <Oxygen/Renderer/Upload/Types.h>

namespace oxygen::engine::upload {

struct UploadPolicy {
  struct Batching {
    static constexpr uint32_t kMaxRegionsPerCommandList = 256;
    static constexpr uint64_t kMaxBytesPerCommandList
      = 64ull * 1024ull * 1024ull; // 64 MB
  };
  struct AlignmentPolicy {
    // D3D12_TEXTURE_DATA_PITCH_ALIGNMENT / Vulkan row pitch alignment (bytes)
    static constexpr Alignment kRowPitchAlignment { 256u };
    // D3D12_TEXTURE_DATA_PLACEMENT_ALIGNMENT (bytes)
    static constexpr Alignment kPlacementAlignment { 512u };
    // Generic buffer copy alignment (bytes)
    static constexpr oxygen::engine::upload::Alignment kBufferCopyAlignment {
      256u
    };
  };
  struct Limits {
    static constexpr uint64_t kSmallArenaBlockMin = 64ull * 1024ull; // 64 KB
    static constexpr uint64_t kSmallArenaBlockMax
      = 1ull * 1024ull * 1024ull; // 1 MB
  };
  struct Timeouts {
    static constexpr uint32_t kFlushTimeSliceMs = 2u;
  };
};

OXGN_RNDR_API auto DefaultUploadPolicy() -> UploadPolicy;

} // namespace oxygen::engine::upload
