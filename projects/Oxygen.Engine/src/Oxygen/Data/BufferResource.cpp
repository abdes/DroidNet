//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <bit>
#include <cstdint>
#include <utility>

#include <fmt/format.h>

#include <Oxygen/Core/Detail/FormatUtils.h>
#include <Oxygen/Core/Types/Format.h>
#include <Oxygen/Data/BufferResource.h>
#include <Oxygen/Data/CookedOpenError.h>
#include <Oxygen/Data/PakFormat_core.h>
#include <Oxygen/Serio/ByteBuffer.h>

using oxygen::data::BufferResource;

namespace {

using oxygen::Format;
using oxygen::data::CookedDefect;
using oxygen::data::CookedOpenError;
using Usage = BufferResource::UsageFlags;

//! Every usage flag the format defines.
constexpr auto kKnownUsage = Usage::kVertexBuffer | Usage::kIndexBuffer
  | Usage::kConstantBuffer | Usage::kStorageBuffer | Usage::kIndirectBuffer
  | Usage::kCPUWritable | Usage::kCPUReadable | Usage::kDynamic | Usage::kStatic
  | Usage::kImmutable;

//! The update frequencies, of which a buffer has at most one.
constexpr auto kFrequencies
  = Usage::kDynamic | Usage::kStatic | Usage::kImmutable;

auto CheckUsage(const uint32_t flags) -> void
{
  const auto unknown = flags & ~std::to_underlying(kKnownUsage);
  if (unknown != 0U) {
    throw CookedOpenError(CookedDefect::kInvalidValue,
      fmt::format("buffer usage flags 0x{:08X} are unknown", unknown));
  }
  const auto frequencies = flags & std::to_underlying(kFrequencies);
  if (std::popcount(frequencies) > 1) {
    throw CookedOpenError(CookedDefect::kInconsistent,
      fmt::format(
        "buffer usage flags 0x{:08X} name more than one update frequency",
        flags));
  }
}

//! Checks that a formatted buffer holds whole elements of a format a buffer
//! can hold: one texel per element, not a block of them.
auto CheckFormatted(const oxygen::data::pak::core::BufferResourceDesc& desc)
  -> void
{
  const auto& info = oxygen::graphics::detail::GetFormatInfo(
    static_cast<Format>(desc.element_format));
  const auto holds_elements
    = info.bytes_per_block != 0U && info.block_size == 1U;
  if (!holds_elements) {
    throw CookedOpenError(CookedDefect::kInvalidValue,
      fmt::format("buffer element format {} is unknown or block compressed",
        desc.element_format));
  }
  if (desc.element_stride != 0U) {
    throw CookedOpenError(CookedDefect::kInconsistent,
      fmt::format("formatted buffer has a stride of {}; its format sets it",
        desc.element_stride));
  }
  if (desc.size_bytes % info.bytes_per_block != 0U) {
    throw CookedOpenError(CookedDefect::kSizeMismatch,
      fmt::format("buffer of {} bytes holds part of a {}-byte element",
        desc.size_bytes, info.bytes_per_block));
  }
}

//! Checks that a structured or raw buffer holds whole `element_stride`-byte
//! elements; an empty one needs no stride.
auto CheckStructured(const oxygen::data::pak::core::BufferResourceDesc& desc)
  -> void
{
  if (desc.size_bytes == 0U) {
    return;
  }
  if (desc.element_stride == 0U) {
    throw CookedOpenError(CookedDefect::kInvalidValue,
      "a buffer without an element format needs an element stride");
  }
  if (desc.size_bytes % desc.element_stride != 0U) {
    throw CookedOpenError(CookedDefect::kSizeMismatch,
      fmt::format("buffer of {} bytes holds part of a {}-byte element",
        desc.size_bytes, desc.element_stride));
  }
}

} // namespace

/*!
 Creates a BufferResource instance from a PAK descriptor and the buffer that
 holds its payload, which it owns.

 @param desc  Buffer resource descriptor from the PAK file.
 @param data  Raw buffer bytes; ownership is transferred (moved in).
              A loader reads the payload straight into it.

### Usage Example

 ```cpp
 pak::core::BufferResourceDesc desc { .data_offset = 0,
   .size_bytes = sizeof(uint32_t) * index_count,
   .usage_flags = static_cast<uint32_t>(
     BufferResource::UsageFlags::kIndexBuffer),
   .element_stride = sizeof(uint32_t),
   .element_format = 0, // structured / index buffer
   .reserved = {} };
 auto bytes = serio::ByteBuffer::Allocate(serio::CountOf(desc.size_bytes));
 // ... read the payload into bytes->Bytes() ...
 auto resource = BufferResource(desc, std::move(bytes).value());
 ```

 @see pak::core::BufferResourceDesc, to_string(BufferResource::UsageFlags)
*/
BufferResource::BufferResource(
  pak::core::BufferResourceDesc desc, serio::ByteBuffer data)
  : desc_(desc)
  , data_(std::move(data))
{
  CheckUsage(desc_.usage_flags);
  if (desc_.element_format != 0U) {
    CheckFormatted(desc_);
  } else {
    CheckStructured(desc_);
  }
  if (data_.Bytes().size() != desc_.size_bytes) {
    throw CookedOpenError(CookedDefect::kSizeMismatch,
      fmt::format("buffer row names {} bytes, its payload holds {}",
        desc_.size_bytes, data_.Bytes().size()));
  }
}

auto BufferResource::GetElementFormat() const noexcept -> Format
{
  return static_cast<Format>(desc_.element_format);
}
