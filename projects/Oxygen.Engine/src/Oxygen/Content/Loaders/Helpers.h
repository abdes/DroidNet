//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstdint>
#include <cstring>
#include <iterator>
#include <stdexcept>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include <fmt/format.h>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Base/NoStd.h>
#include <Oxygen/Content/Internal/DependencyCollector.h>
#include <Oxygen/Content/Internal/ResourceRef.h>
#include <Oxygen/Content/LoaderContext.h>
#include <Oxygen/Data/AssetReferences.h>
#include <Oxygen/Data/AssetType.h>
#include <Oxygen/Data/CookedFormatError.h>
#include <Oxygen/Data/DescriptorStrings.h>
#include <Oxygen/Data/PakFormatSerioLoaders.h>
#include <Oxygen/Data/TextureResource.h>
#include <Oxygen/Serio/ByteBuffer.h>
#include <Oxygen/Serio/Reader.h>
#include <Oxygen/Serio/Writer.h>

namespace oxygen::content::loaders {

//! Takes the payload a resource loader was handed, checking that it holds the
//! `size_bytes` its descriptor names; `what` names the resource in errors.
inline auto TakePayload(const LoaderContext& context, const uint64_t size_bytes,
  const char* what) -> serio::ByteBuffer
{
  if (size_bytes == 0U) {
    return {};
  }
  if (!context.payload || context.payload->Size().get() != size_bytes) {
    throw data::CookedFormatError(data::CookedDefect::kSizeMismatch,
      fmt::format("error reading {}: payload of {} bytes was not read", what,
        size_bytes));
  }
  return std::move(*context.payload);
}

inline auto AddRangeEnd(size_t& end, const size_t offset, const size_t size)
  -> void
{
  if (size == 0) {
    end = std::max(end, offset);
    return;
  }

  const size_t candidate = offset + size;
  if (candidate < offset) {
    throw data::CookedFormatError(
      data::CookedDefect::kOutOfBounds, "range overflow");
  }
  end = std::max(end, candidate);
}

//! Refuses a descriptor whose read of `field` failed: it ends before the
//! bytes the read needs.
/*!
 @throw data::CookedFormatError `kTruncated` when `result` holds an error.
*/
inline auto CheckLoaderResult(const oxygen::Result<void>& result,
  const char* subject, const char* field) -> void
{
  if (!result) {
    LOG_F(ERROR, "-failed- on {}: {}", field, result.error().message().c_str());
    throw data::CookedFormatError(data::CookedDefect::kTruncated,
      fmt::format(
        "error reading {} ({}): {}", subject, field, result.error().message()));
  }
}

//! Refuses a descriptor whose read of `field` failed, as the `void` overload.
template <typename T>
inline auto CheckLoaderResult(const oxygen::Result<T>& result,
  const char* subject, const char* field) -> void
{
  if (!result) {
    LOG_F(ERROR, "-failed- on {}: {}", field, result.error().message().c_str());
    throw data::CookedFormatError(data::CookedDefect::kTruncated,
      fmt::format(
        "error reading {} ({}): {}", subject, field, result.error().message()));
  }
}

//=== Cooked IO Helpers ===--------------------------------------------------//

[[nodiscard]] inline auto ReadUnorm16(
  serio::AnyReader& reader, data::Unorm16& out) noexcept -> Result<void>
{
  uint16_t u_value = 0;
  auto result = reader.ReadInto(u_value);
  if (!result) {
    return ::oxygen::Err(result.error());
  }
  out = data::Unorm16 { u_value };
  return {};
}

[[nodiscard]] inline auto ReadHalfFloat(
  serio::AnyReader& reader, data::HalfFloat& out) noexcept -> Result<void>
{
  uint16_t u_value = 0;
  auto result = reader.ReadInto(u_value);
  if (!result) {
    return ::oxygen::Err(result.error());
  }
  out = data::HalfFloat { u_value };
  return {};
}

[[nodiscard]] inline auto WriteUnorm16(
  serio::AnyWriter& writer, const data::Unorm16 value) noexcept -> Result<void>
{
  const auto u_value = value.get();
  return writer.Write(u_value);
}

[[nodiscard]] inline auto WriteHalfFloat(serio::AnyWriter& writer,
  const data::HalfFloat value) noexcept -> Result<void>
{
  const auto u_value = value.get();
  return writer.Write(u_value);
}

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

 @see oxygen::data::pak::core::AssetHeader
*/
inline auto LoadAssetHeader(
  serio::AnyReader& reader, data::pak::core::AssetHeader& header) -> void
{
  using data::AssetType;
  using data::pak::core::AssetHeader;

  LOG_SCOPE_F(1, "Header");

  auto result = reader.ReadInto<AssetHeader>(header);
  if (!result) {
    LOG_F(
      INFO, "-failed- on AssetHeader: {}", result.error().message().c_str());
    throw data::CookedFormatError(data::CookedDefect::kTruncated,
      fmt::format("error reading asset header: {}", result.error().message()));
  }

  // Check validity of the header
  auto asset_type = header.asset_type;
  if (asset_type > static_cast<uint8_t>(AssetType::kMaxAssetType)) {
    LOG_F(ERROR, "-failed- on invalid asset type {}", asset_type);
    throw data::CookedFormatError(data::CookedDefect::kInvalidValue,
      fmt::format("invalid asset type in header: {}", asset_type));
  }
  LOG_F(1, "asset type         : {}",
    nostd::to_string(static_cast<AssetType>(asset_type)));

  LOG_F(1, "string table       : {} bytes at {}", header.strings.size,
    header.strings.offset);
  LOG_F(1, "name offset        : {}", header.name_offset);

  LOG_F(1, "format version     : {}", header.version);
  LOG_F(1, "variant flags      : 0x{:08X}", header.variant_flags);
  LOG_F(1, "streaming priority : {}", header.streaming_priority);
  auto content_hash_hex = std::string {};
  content_hash_hex.reserve(header.content_hash.size() * 2);
  for (const auto byte : header.content_hash) {
    fmt::format_to(std::back_inserter(content_hash_hex), "{:02X}", byte);
  }
  LOG_F(1, "content hash       : {}", content_hash_hex);
}

//! Reads the string table of the descriptor that starts at `descriptor_start`.
/*!
 Leaves the reader where it was.

 @throw data::CookedFormatError `kOutOfBounds` when the table is empty or starts
 within the asset header, `kTruncated` when it runs past the descriptor, and
 `kInvalidString` when it is malformed.
*/
inline auto ReadDescriptorStrings(serio::AnyReader& reader,
  const size_t descriptor_start, const data::pak::core::AssetHeader& header)
  -> data::DescriptorStrings
{
  const auto& table = header.strings;
  if (table.size == 0 || table.offset < sizeof(data::pak::core::AssetHeader)) {
    throw data::CookedFormatError(data::CookedDefect::kOutOfBounds,
      "descriptor string table location is invalid");
  }
  const auto position = reader.Position();
  CheckLoaderResult(position, "descriptor", "position");
  CheckLoaderResult(reader.Seek(descriptor_start + table.offset), "descriptor",
    "string table offset");
  auto bytes = reader.ReadBlob(table.size);
  CheckLoaderResult(bytes, "descriptor", "string table");
  CheckLoaderResult(reader.Seek(*position), "descriptor", "position");

  auto chars = std::vector<char>(table.size);
  std::memcpy(chars.data(), bytes->data(), table.size);
  try {
    return data::DescriptorStrings { std::move(chars) };
  } catch (const std::invalid_argument& error) {
    throw data::CookedFormatError(
      data::CookedDefect::kInvalidString, error.what());
  }
}

//! Moves past the string table that follows a descriptor's records.
/*!
 Loaders consume their whole descriptor. A descriptor whose string table
 follows its records calls this after reading them.

 @throw data::CookedFormatError `kSizeMismatch` when the records do not end
 where the table starts, `kTruncated` when the table runs past the
 descriptor.
*/
inline auto SkipDescriptorStrings(serio::AnyReader& reader,
  const size_t descriptor_start, const data::pak::core::AssetHeader& header)
  -> void
{
  const auto position = reader.Position();
  CheckLoaderResult(position, "descriptor", "position");
  if (*position != descriptor_start + header.strings.offset) {
    throw data::CookedFormatError(data::CookedDefect::kSizeMismatch,
      "descriptor records do not end where its string table starts");
  }
  CheckLoaderResult(
    reader.Forward(header.strings.size), "descriptor", "string table");
}

//! Resolves a string of a descriptor's string table.
/*!
 @throw data::CookedFormatError `kOutOfBounds`, naming `field`, when the offset
 is not in the table.
*/
inline auto ResolveDescriptorString(const data::DescriptorStrings& strings,
  const data::pak::core::StringTableOffsetT offset, const char* field)
  -> std::string
{
  const auto value = strings.TryGet(offset);
  if (!value) {
    throw data::CookedFormatError(data::CookedDefect::kOutOfBounds,
      fmt::format("{} offset {} is outside the string table", field, offset));
  }
  return std::string { *value };
}

//! Collects the local texture `reference` names, which the asset's load then
//! loads and holds; a texture of another source is bound by key when the
//! asset publishes, and the fallback and error textures are no dependency.
/*!
 Reading the asset's references checked that every local texture binding lies
 inside its source's texture table.
*/
inline auto CollectTexture(const LoaderContext& context,
  const data::ResourceReferenceIndex reference) -> void
{
  const auto target = context.ResolveTexture(reference);
  const auto* index = std::get_if<data::pak::core::ResourceIndexT>(&target);
  if (index == nullptr || *index == data::pak::core::kFallbackResourceIndex
    || *index == data::pak::core::kErrorTextureResourceIndex) {
    return;
  }
  context.dependency_collector->AddResourceDependency(internal::ResourceRef {
    .source = context.source_instance,
    .resource_type_id = data::TextureResource::ClassTypeId(),
    .resource_index = *index,
  });
}

} // namespace oxygen::content::loaders
