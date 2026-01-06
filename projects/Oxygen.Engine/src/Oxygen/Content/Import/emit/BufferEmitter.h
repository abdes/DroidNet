//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstdint>
#include <span>

#include <Oxygen/Content/Import/emit/ResourceAppender.h>

namespace oxygen::content::import::emit {

//! Gets or creates a buffer resource index.
/*!
 Uses signature-based deduplication to avoid storing identical buffers.

 @param state The buffer emission state.
 @param bytes The buffer data.
 @param alignment Alignment for the buffer data.
 @param usage_flags Buffer usage flags.
 @param element_stride Stride of each element (0 for index buffers).
 @param element_format Format of elements (for typed buffers).
 @return The buffer resource index.
*/
[[nodiscard]] auto GetOrCreateBufferResourceIndex(BufferEmissionState& state,
  std::span<const std::byte> bytes, uint64_t alignment, uint32_t usage_flags,
  uint32_t element_stride, uint8_t element_format) -> uint32_t;

} // namespace oxygen::content::import::emit
