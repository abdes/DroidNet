//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Base/NoStd.h>
#include <Oxygen/Data/AssetKey.h>
#include <Oxygen/Data/AssetType.h>
#include <Oxygen/Data/PakFormat.h>
#include <Oxygen/Serio/Reader.h>

#include <algorithm>

namespace oxygen::serio {

//! Load specialization for Person.
inline auto Load(AnyReader& reader, data::pak::AssetHeader& header)
  -> Result<void>
{
  auto pack = reader.ScopedAlignment(1);
  CHECK_RESULT(reader.ReadInto(header.asset_type));
  CHECK_RESULT(
    reader.ReadBlobInto(std::span(reinterpret_cast<std::byte*>(header.name),
      oxygen::data::pak::kMaxNameSize)));
  CHECK_RESULT(reader.ReadInto(header.version));
  CHECK_RESULT(reader.ReadInto(header.streaming_priority));
  CHECK_RESULT(reader.ReadInto(header.content_hash));
  CHECK_RESULT(reader.ReadInto(header.variant_flags));
  CHECK_RESULT(reader.Forward(sizeof(header.reserved)));
  return {};
}

//! Load specialization for Person.
inline auto Load(AnyReader& reader, data::pak::BufferResourceDesc& desc)
  -> Result<void>
{
  auto pack = reader.ScopedAlignment(1);
  CHECK_RESULT(reader.ReadInto(desc.data_offset));
  CHECK_RESULT(reader.ReadInto(desc.size_bytes));
  CHECK_RESULT(reader.ReadInto(desc.usage_flags));
  CHECK_RESULT(reader.ReadInto(desc.element_stride));
  CHECK_RESULT(reader.ReadInto(desc.element_format));
  CHECK_RESULT(reader.Forward(sizeof(desc.reserved)));
  return {};
}

//! Load specialization for Person.
inline auto Load(AnyReader& reader, data::pak::MeshViewDesc& desc)
  -> Result<void>
{
  auto pack = reader.ScopedAlignment(1);
  CHECK_RESULT(reader.ReadInto(desc.first_index));
  CHECK_RESULT(reader.ReadInto(desc.index_count));
  CHECK_RESULT(reader.ReadInto(desc.first_vertex));
  CHECK_RESULT(reader.ReadInto(desc.vertex_count));
  return {};
}

//! Load specialization for Person.
inline auto Load(AnyReader& reader, data::pak::TextureResourceDesc& desc)
  -> Result<void>
{
  auto pack = reader.ScopedAlignment(1);
  CHECK_RESULT(reader.ReadInto(desc.data_offset));
  CHECK_RESULT(reader.ReadInto(desc.data_size));
  CHECK_RESULT(reader.ReadInto(desc.texture_type));
  CHECK_RESULT(reader.ReadInto(desc.compression_type));
  CHECK_RESULT(reader.ReadInto(desc.width));
  CHECK_RESULT(reader.ReadInto(desc.height));
  CHECK_RESULT(reader.ReadInto(desc.depth));
  CHECK_RESULT(reader.ReadInto(desc.array_layers));
  CHECK_RESULT(reader.ReadInto(desc.mip_levels));
  CHECK_RESULT(reader.ReadInto(desc.format));
  CHECK_RESULT(reader.ReadInto(desc.alignment));
  CHECK_RESULT(reader.ReadInto(desc.is_cubemap));
  CHECK_RESULT(reader.Forward(sizeof(desc.reserved)));
  return {};
}

//! Load specialization for ShaderReferenceDesc.
inline auto Load(AnyReader& reader, data::pak::ShaderReferenceDesc& desc)
  -> Result<void>
{
  auto pack = reader.ScopedAlignment(1);
  CHECK_RESULT(reader.ReadBlobInto(
    std::span(reinterpret_cast<std::byte*>(desc.shader_unique_id),
      std::size(desc.shader_unique_id))));
  CHECK_RESULT(reader.ReadInto(desc.shader_hash));
  CHECK_RESULT(reader.ReadBlobInto(std::span(
    reinterpret_cast<std::byte*>(desc.reserved), std::size(desc.reserved))));
  return {};
}

//! Load specialization for Person.
inline auto Load(AnyReader& reader, data::pak::PakHeader& header)
  -> Result<void>
{
  auto pack = reader.ScopedAlignment(1);
  CHECK_RESULT(reader.ReadBlobInto(std::span(
    reinterpret_cast<std::byte*>(header.magic), std::size(header.magic))));
  CHECK_RESULT(reader.ReadInto(header.version));
  CHECK_RESULT(reader.ReadInto(header.content_version));
  CHECK_RESULT(reader.Forward(std::size(header.reserved)));
  return {};
}

//! Load specialization for ResourceRegion.
inline auto Load(AnyReader& reader, data::pak::ResourceRegion& region)
  -> Result<void>
{
  auto pack = reader.ScopedAlignment(1);
  CHECK_RESULT(reader.ReadInto(region.offset));
  CHECK_RESULT(reader.ReadInto(region.size));
  return {};
}

//! Load specialization for ResourceTable.
inline auto Load(AnyReader& reader, data::pak::ResourceTable& table)
  -> Result<void>
{
  auto pack = reader.ScopedAlignment(1);
  CHECK_RESULT(reader.ReadInto(table.offset));
  CHECK_RESULT(reader.ReadInto(table.count));
  CHECK_RESULT(reader.ReadInto(table.entry_size));
  return {};
}

//! Load specialization for PakFooter.
inline auto Load(AnyReader& reader, data::pak::PakFooter& footer)
  -> Result<void>
{
  auto pack = reader.ScopedAlignment(1);
  CHECK_RESULT(reader.ReadInto(footer.directory_offset));
  CHECK_RESULT(reader.ReadInto(footer.directory_size));
  CHECK_RESULT(reader.ReadInto(footer.asset_count));
  CHECK_RESULT(reader.ReadInto(footer.texture_region));
  CHECK_RESULT(reader.ReadInto(footer.buffer_region));
  CHECK_RESULT(reader.ReadInto(footer.audio_region));
  CHECK_RESULT(reader.ReadInto(footer.texture_table));
  CHECK_RESULT(reader.ReadInto(footer.buffer_table));
  CHECK_RESULT(reader.ReadInto(footer.audio_table));
  CHECK_RESULT(
    reader.ReadBlobInto(std::span(reinterpret_cast<std::byte*>(footer.reserved),
      std::size(footer.reserved))));
  CHECK_RESULT(reader.ReadInto(footer.pak_crc32));
  CHECK_RESULT(reader.ReadBlobInto(
    std::span(reinterpret_cast<std::byte*>(footer.footer_magic),
      std::size(footer.footer_magic))));
  return {};
}

//! Load specialization for AssetKey.
inline auto Load(AnyReader& reader, data::AssetKey& key) -> Result<void>
{
  auto pack = reader.ScopedAlignment(1);
  CHECK_RESULT(reader.ReadBlobInto(
    std::span(reinterpret_cast<std::byte*>(key.guid.data()), key.guid.size())));
  return {};
}

//! Load specialization for AssetDirectoryEntry.
inline auto Load(AnyReader& reader, data::pak::AssetDirectoryEntry& entry)
  -> Result<void>
{
  auto pack = reader.ScopedAlignment(1);
  CHECK_RESULT(reader.ReadInto(entry.asset_key));
  CHECK_RESULT(reader.ReadInto(entry.asset_type));
  CHECK_RESULT(reader.ReadInto(entry.entry_offset));
  CHECK_RESULT(reader.ReadInto(entry.desc_offset));
  CHECK_RESULT(reader.ReadInto(entry.desc_size));
  CHECK_RESULT(reader.ReadBlobInto(std::span(
    reinterpret_cast<std::byte*>(entry.reserved), std::size(entry.reserved))));
  return {};
}

} // namespace oxygen::serio

namespace oxygen::content::loaders {

//! Loads an AssetHeader, using the provided \b reader.
/*!

 @tparam S Stream type
 @param reader The reader to use for loading
 @return The loaded AssetHeader

 ### Performance Characteristics

 - Time Complexity: O(1)
 - Memory: No heap allocation
 - Optimization: Reads fields individually to avoid padding/representation
 issues

 @see oxygen::data::pak::AssetHeader
*/
inline auto LoadAssetHeader(
  serio::AnyReader& reader, data::pak::AssetHeader& header) -> void
{
  using data::AssetType;
  using data::pak::AssetHeader;

  LOG_SCOPE_F(INFO, "Header");

  auto result = reader.ReadInto<AssetHeader>(header);
  if (!result) {
    LOG_F(INFO, "-failed- on AssetHeader: {}", result.error().message());
    throw std::runtime_error(
      fmt::format("error reading asset header: {}", result.error().message()));
  }

  // Check validity of the header
  auto asset_type = header.asset_type;
  if (asset_type > static_cast<uint8_t>(AssetType::kMaxAssetType)) {
    LOG_F(INFO, "-failed- on invalid asset type {}", asset_type);
    throw std::runtime_error(
      fmt::format("invalid asset type in header: {}", asset_type));
  }
  LOG_F(INFO, "asset type         : {}",
    nostd::to_string(static_cast<AssetType>(asset_type)));

  // Check that name contains a null terminator
  const auto& name = header.name;
  if (std::ranges::find(name, '\0') == std::end(name)) {
    // We let it go, because the name getter will handle this case and return a
    // valid std::string_view. But we log a warning to help debugging.
    LOG_F(WARNING, "-fishy- on name not null-terminated");
  }
  std::string_view name_view(name, data::pak::kMaxNameSize);
  LOG_F(INFO, "asset name         : {}", name_view);

  LOG_F(INFO, "format version     : {}", header.version);
  LOG_F(INFO, "variant flags      : 0x{:08X}", header.variant_flags);
  LOG_F(INFO, "streaming priority : {}", header.streaming_priority);
  LOG_F(INFO, "content hash       : 0x{:016X}", header.content_hash);
}

} // namespace oxygen::content::loaders
