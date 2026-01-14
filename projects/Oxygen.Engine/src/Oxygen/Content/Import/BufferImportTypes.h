//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace oxygen::content::import {

//! Cooked buffer payload for async emission.
/*!
 Contains all metadata needed by the runtime to create a buffer resource,
 plus the raw buffer data bytes.

 ### Field Descriptions

 - `data`: The raw buffer bytes to be written to `buffers.data`.
 - `alignment`: Required alignment for this buffer (e.g., 16 for vertex buffers,
   4 for index buffers). Used for D3D12 GPU upload alignment.
 - `usage_flags`: Buffer usage hints:
   - `0x01`: Vertex buffer
   - `0x02`: Index buffer
   - `0x04`: Constant buffer
   - `0x08`: Structured buffer
   - `0x10`: Raw buffer
 - `element_stride`: Stride of each element (0 for raw/index buffers).
 - `element_format`: Format enum value (0 = raw or structured).
 - `content_hash`: First 8 bytes of SHA256 of buffer data (for deduplication).

 @see BufferResourceDesc for on-disk format
*/
struct CookedBufferPayload {
  //! Raw buffer data bytes.
  std::vector<std::byte> data;

  //! Required alignment for this buffer (defaults to 16).
  uint64_t alignment = 16;

  //! Buffer usage flags (vertex, index, constant, etc.).
  uint32_t usage_flags = 0;

  //! Element stride (0 for raw buffers, >0 for structured).
  uint32_t element_stride = 0;

  //! Element format enum (0 = raw or structured).
  uint8_t element_format = 0;

  //! Content hash for deduplication (first 8 bytes of SHA256).
  uint64_t content_hash = 0;
};

} // namespace oxygen::content::import
