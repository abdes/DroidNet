//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <fmt/format.h>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Base/NoStd.h>
#include <Oxygen/Content/AssetLoader.h>
#include <Oxygen/Data/PakFormatSerioLoaders.h>
#include <Oxygen/Serio/Reader.h>
#include <Oxygen/Serio/Writer.h>

namespace oxygen::content::loaders {

inline auto AddRangeEnd(size_t& end, const size_t offset, const size_t size)
  -> void
{
  if (size == 0) {
    end = std::max(end, offset);
    return;
  }

  const size_t candidate = offset + size;
  if (candidate < offset) {
    throw std::runtime_error("range overflow");
  }
  end = std::max(end, candidate);
}

inline auto CheckLoaderResult(const oxygen::Result<void>& result,
  const char* subject, const char* field) -> void
{
  if (!result) {
    LOG_F(ERROR, "-failed- on {}: {}", field, result.error().message().c_str());
    throw std::runtime_error(fmt::format(
      "error reading {} ({}): {}", subject, field, result.error().message()));
  }
}

template <typename T>
inline auto CheckLoaderResult(const oxygen::Result<T>& result,
  const char* subject, const char* field) -> void
{
  if (!result) {
    LOG_F(ERROR, "-failed- on {}: {}", field, result.error().message().c_str());
    throw std::runtime_error(fmt::format(
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

 @see oxygen::data::pak::AssetHeader
*/
inline auto LoadAssetHeader(
  serio::AnyReader& reader, data::pak::AssetHeader& header) -> void
{
  using data::AssetType;
  using data::pak::AssetHeader;

  LOG_SCOPE_F(1, "Header");

  auto result = reader.ReadInto<AssetHeader>(header);
  if (!result) {
    LOG_F(
      INFO, "-failed- on AssetHeader: {}", result.error().message().c_str());
    throw std::runtime_error(
      fmt::format("error reading asset header: {}", result.error().message()));
  }

  // Check validity of the header
  auto asset_type = header.asset_type;
  if (asset_type > static_cast<uint8_t>(AssetType::kMaxAssetType)) {
    LOG_F(ERROR, "-failed- on invalid asset type {}", asset_type);
    throw std::runtime_error(
      fmt::format("invalid asset type in header: {}", asset_type));
  }
  LOG_F(1, "asset type         : {}",
    nostd::to_string(static_cast<AssetType>(asset_type)));

  // Check that name contains a null terminator
  const auto& name = header.name;
  if (std::ranges::find(name, '\0') == std::end(name)) {
    // We let it go, because the name getter will handle this case and return a
    // valid std::string_view. But we log a warning to help debugging.
    LOG_F(WARNING, "-fishy- on name not null-terminated");
  }
  std::string_view name_view(name, data::pak::kMaxNameSize);
  LOG_F(1, "asset name         : {}", name_view);

  LOG_F(1, "format version     : {}", header.version);
  LOG_F(1, "variant flags      : 0x{:08X}", header.variant_flags);
  LOG_F(1, "streaming priority : {}", header.streaming_priority);
  LOG_F(1, "content hash       : 0x{:016X}", header.content_hash);
}

//! Helper RAII class for automatic resource cleanup when an error occurs during
//! loading.
class ResourceCleanupGuard {
  AssetLoader* loader_;
  ResourceKey key_;
  bool disabled_;

public:
  ResourceCleanupGuard(AssetLoader& loader, ResourceKey key)
    : loader_(&loader)
    , key_(key)
    , disabled_(false)
  {
  }
  ~ResourceCleanupGuard()
  {
    if (!disabled_ && loader_)
      loader_->ReleaseResource(key_);
  }
  void disable() { disabled_ = true; }
};

} // namespace oxygen::content::loaders
