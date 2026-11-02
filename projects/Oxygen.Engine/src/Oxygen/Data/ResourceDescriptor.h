//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <Oxygen/Base/Result.h>
#include <Oxygen/Data/CookedOpenError.h>
#include <Oxygen/Data/PakFormatVersions.h>
#include <Oxygen/Data/PakFormat_core.h>
#include <Oxygen/Data/api_export.h>
#include <Oxygen/Serio/Record.h>

//! Loose resource descriptors: the `.otex` and `.obuf` sidecars.
/*!
 @file ResourceDescriptor.h

 A loose cooked root names a texture or buffer by a small sidecar file at its
 virtual path. The sidecar holds the resource's row in its source's resource
 table and a copy of that row; a buffer sidecar also holds the buffer's named
 views. Every sidecar starts with a `ResourceDescriptorHeader`.

 Layouts, packed and little-endian:

 - `.otex`: `TextureResourceDescriptorRecord`.
 - `.obuf`: `BufferResourceDescriptorRecord`, then `view_count`
   `BufferViewRecord`s, then a string table of `strings_size` bytes holding
   the view names.
*/

namespace oxygen::data {

inline constexpr auto kTextureResourceDescriptorMagic
  = std::array { 'O', 'T', 'E', 'X' };
inline constexpr uint16_t kTextureResourceDescriptorVersion
  = pak::version::kTextureResourceDescriptorVersion;

inline constexpr auto kBufferResourceDescriptorMagic
  = std::array { 'O', 'B', 'U', 'F' };
inline constexpr uint16_t kBufferResourceDescriptorVersion
  = pak::version::kBufferResourceDescriptorVersion;

//! The view every buffer descriptor holds: the whole buffer.
inline constexpr std::string_view kWholeBufferViewName = "__all__";

#pragma pack(push, 1)
//! The start of every resource descriptor.
struct ResourceDescriptorHeader {
  std::array<char, 4> magic {};
  //! The resource's row in its source's resource table; 4-byte aligned in
  //! the file.
  pak::core::ResourceIndexT index = pak::core::kNoResourceIndex;
  uint16_t version = 0;
};

//! A `.otex` file.
struct TextureResourceDescriptorRecord {
  ResourceDescriptorHeader header {};
  pak::core::TextureResourceDesc descriptor {};
};

//! The fixed part of a `.obuf` file.
struct BufferResourceDescriptorRecord {
  ResourceDescriptorHeader header {};
  pak::core::BufferResourceDesc descriptor {};
  uint32_t view_count = 0;
  //! Size of the string table that follows the view records.
  pak::core::StringTableSizeT strings_size = 0;
};

//! One named view of a `.obuf` buffer.
struct BufferViewRecord {
  uint64_t byte_offset = 0;
  uint64_t byte_length = 0;
  uint64_t element_offset = 0;
  uint64_t element_count = 0;
  //! The view's name, in the string table.
  pak::core::StringTableOffsetT name_offset = 0;
};
#pragma pack(pop)

static_assert(sizeof(ResourceDescriptorHeader) == 10);
static_assert(sizeof(TextureResourceDescriptorRecord) == 73);
static_assert(sizeof(BufferResourceDescriptorRecord) == 71);
static_assert(sizeof(BufferViewRecord) == 36);

//! The size of every `.otex` file.
inline constexpr std::size_t kTextureResourceDescriptorSize
  = sizeof(TextureResourceDescriptorRecord);

//! A decoded `.otex` file.
struct TextureResourceDescriptor {
  pak::core::ResourceIndexT index { pak::core::kNoResourceIndex };
  pak::core::TextureResourceDesc descriptor {};
};

//! A named range of a buffer.
struct BufferResourceView {
  std::string name;
  uint64_t byte_offset = 0;
  uint64_t byte_length = 0;
  uint64_t element_offset = 0;
  uint64_t element_count = 0;

  auto operator==(const BufferResourceView&) const -> bool = default;
};

//! A decoded `.obuf` file.
/*!
 View names are non-empty and distinct, and one of them is
 `kWholeBufferViewName`, which equals `WholeBufferView` of the descriptor. The
 buffer has a nonzero `BufferElementStride`; every other view lies within the
 buffer, starts and ends on that stride, and its element range is its byte
 range divided by it.
*/
struct BufferResourceDescriptor {
  pak::core::ResourceIndexT index { pak::core::kNoResourceIndex };
  pak::core::BufferResourceDesc descriptor {};
  std::vector<BufferResourceView> views;
};

//! The size of one element of the buffer `descriptor` describes: its
//! `element_stride`, else the size of its `element_format`, else 1 for a raw
//! buffer; 0 when its format has no fixed per-element size.
OXGN_DATA_NDAPI auto BufferElementStride(
  const pak::core::BufferResourceDesc& descriptor) -> uint64_t;

//! The `kWholeBufferViewName` view of the buffer `descriptor` describes; a
//! buffer of no fixed element size counts its elements as bytes.
OXGN_DATA_NDAPI auto WholeBufferView(
  const pak::core::BufferResourceDesc& descriptor) -> BufferResourceView;

//! Encodes a `.otex` file.
OXGN_DATA_NDAPI auto EncodeTextureResourceDescriptor(
  const TextureResourceDescriptor& descriptor) -> std::vector<std::byte>;

//! Decodes a `.otex` file.
/*!
 @return `CookedOpenFailure::kWrongFormat` for bytes that do not start with a
   texture descriptor's magic, `kUnsupportedVersion` for another version and
   `kCorrupt` for any other size.
*/
OXGN_DATA_NDAPI auto DecodeTextureResourceDescriptor(
  std::span<const std::byte> bytes)
  -> Result<TextureResourceDescriptor, CookedOpenError>;

//! Encodes a `.obuf` file.
/*!
 @throw std::invalid_argument when the views break the
 `BufferResourceDescriptor` rules, or a view name holds `\0` or is not UTF-8.
*/
OXGN_DATA_NDAPI auto EncodeBufferResourceDescriptor(
  const BufferResourceDescriptor& descriptor) -> std::vector<std::byte>;

//! Decodes a `.obuf` file.
/*!
 @return `CookedOpenFailure::kWrongFormat` for bytes that do not start with a
   buffer descriptor's magic, `kUnsupportedVersion` for another version and
   `kCorrupt` for a size its counts do not account for, a malformed string
   table, or views `EncodeBufferResourceDescriptor` refuses.
*/
OXGN_DATA_NDAPI auto DecodeBufferResourceDescriptor(
  std::span<const std::byte> bytes)
  -> Result<BufferResourceDescriptor, CookedOpenError>;

} // namespace oxygen::data

// clang-format off
namespace oxygen::serio {
template <> inline constexpr bool kIsRecord<data::ResourceDescriptorHeader> = true;
template <> inline constexpr bool kIsRecord<data::TextureResourceDescriptorRecord> = true;
template <> inline constexpr bool kIsRecord<data::BufferResourceDescriptorRecord> = true;
template <> inline constexpr bool kIsRecord<data::BufferViewRecord> = true;
} // namespace oxygen::serio
// clang-format on
