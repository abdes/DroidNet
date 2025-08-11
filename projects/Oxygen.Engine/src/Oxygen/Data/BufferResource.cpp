//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstdint>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Data/BufferResource.h>

using oxygen::data::BufferResource;

/*!
 Creates a BufferResource instance from a PAK descriptor and an owned
 byte vector containing the buffer payload.

 The constructor validates several structural invariants (debug builds via
 DCHECK_* macros) so that malformed PAK content fails fast:

 - **Formatted buffers** (`element_format != 0`): `element_stride` must be 0
   (the format implies element size; stride field is ignored and must be
   zero to avoid ambiguity).
 - **Structured / index buffers** (`element_format == 0` and
   `element_stride > 1`): `size_bytes` must be an exact multiple of
   `element_stride` (no partial trailing element data is allowed).
 - **Raw buffers** (`element_format == 0` and `element_stride == 1`): any
   `size_bytes` is accepted (byte-addressable).
 - `element_stride` must never be zero when `element_format == 0` and the
   buffer is not raw (i.e., stride > 1 case).

 These checks mirror the format rules documented in `PakFormat.h` and help
 maintain consistent interpretation when buffers are later reinterpreted as
 vertex or index data.

 @param desc  Buffer resource descriptor from the PAK file (copied then
              validated). Undefined behavior in release builds if the
              invariants above are violated (debug build will abort).
 @param data  Raw buffer bytes; ownership is transferred (moved in).

### Usage Example

 ```cpp
 pak::BufferResourceDesc desc { .data_offset = 0,
   .size_bytes = sizeof(uint32_t) * index_count,
   .usage_flags = static_cast<uint32_t>(
     BufferResource::UsageFlags::kIndexBuffer),
   .element_stride = sizeof(uint32_t),
   .element_format = 0, // structured / index buffer
   .reserved = {} };
 std::vector<uint8_t> bytes(desc.size_bytes);
 auto resource = BufferResource(desc, std::move(bytes));
 ```

 @see pak::BufferResourceDesc, to_string(BufferResource::UsageFlags)
*/
BufferResource::BufferResource(
  pak::BufferResourceDesc desc, std::vector<uint8_t> data)
  : desc_(std::move(desc))
  , data_(std::move(data))
{
  // Invariants (validated eagerly so mis-authored PAK data fails fast):
  // 1. Formatted buffers (element_format != 0) must have element_stride == 0.
  if (desc_.element_format != 0) {
    DCHECK_EQ_F(desc_.element_stride, 0u,
      "formatted buffer must have zero element_stride (was %u)",
      desc_.element_stride);
  }
  // 2. Structured/raw buffers (element_format == 0) with stride > 1 must have
  //    size_bytes a multiple of element_stride.
  if (desc_.element_format == 0 && desc_.element_stride > 1) {
    DCHECK_F(desc_.element_stride != 0,
      "element_stride cannot be zero for structured buffer");
    DCHECK_F(desc_.size_bytes % desc_.element_stride == 0,
      "Buffer size %u not aligned to element stride %u", desc_.size_bytes,
      desc_.element_stride);
  }
}

auto BufferResource::GetElementFormat() const noexcept -> Format
{
  static_assert(
    static_cast<std::underlying_type_t<Format>>(Format::kUnknown) == 0,
    "Format::kUnknown must be 0 for correct raw buffer detection");

  if (desc_.element_format
      >= static_cast<std::underlying_type_t<Format>>(Format::kUnknown)
    && desc_.element_format
      <= static_cast<std::underlying_type_t<Format>>(Format::kMaxFormat)) {
    return static_cast<Format>(desc_.element_format);
  }
  LOG_F(WARNING, "Invalid element format: {}", desc_.element_format);
  return Format::kUnknown;
}
