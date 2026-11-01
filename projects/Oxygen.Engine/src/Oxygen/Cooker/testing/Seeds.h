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
#include <vector>

#include <Oxygen/Base/Sha256.h>
#include <Oxygen/Base/Uuid.h>
#include <Oxygen/Core/Types/AssetKey.h>
#include <Oxygen/Data/AssetType.h>
#include <Oxygen/Data/PakCatalog.h>
#include <Oxygen/Data/SourceKey.h>

//! Deterministic values a test tells apart by a seed.
namespace oxygen::cooker::testing {

//! `count` bytes of `seed`: the payload of a resource a test tells apart by
//! its seed.
[[nodiscard]] inline auto SeedBytes(const uint8_t seed,
  const size_t count = sizeof(uint32_t)) -> std::vector<std::byte>
{
  return std::vector<std::byte>(count, static_cast<std::byte>(seed));
}

//! The bytes of a valid UUIDv7 derived from `seed`.
[[nodiscard]] inline auto UuidBytesOf(const uint8_t seed)
  -> std::array<uint8_t, Uuid::kSize>
{
  auto bytes = std::array<uint8_t, Uuid::kSize> {};
  for (auto i = size_t { 0U }; i < bytes.size(); ++i) {
    bytes.at(i) = static_cast<uint8_t>(seed + static_cast<uint8_t>(i));
  }
  bytes.at(6) = static_cast<uint8_t>((bytes.at(6) & 0x0FU) | 0x70U);
  bytes.at(8) = static_cast<uint8_t>((bytes.at(8) & 0x3FU) | 0x80U);
  return bytes;
}

//! A valid UUIDv7 derived from `seed`.
[[nodiscard]] inline auto UuidOf(const uint8_t seed) -> Uuid
{
  return Uuid::FromBytes(UuidBytesOf(seed)).value();
}

//! A valid UUIDv7 source identity derived from `seed`.
[[nodiscard]] inline auto SourceKeyOf(const uint8_t seed) -> data::SourceKey
{
  return data::SourceKey::FromBytes(UuidBytesOf(seed)).value();
}

//! The base catalog entry of an asset with no resources, whose descriptor was
//! `descriptor`: its transitive digest is its descriptor's.
[[nodiscard]] inline auto BaseEntry(const data::AssetKey& key,
  const data::AssetType type, const std::span<const std::byte> descriptor)
  -> data::PakCatalogEntry
{
  const auto digest = base::ComputeSha256(descriptor);
  return {
    .asset_key = key,
    .asset_type = type,
    .descriptor_digest = digest,
    .transitive_resource_digest = digest,
  };
}

} // namespace oxygen::cooker::testing
