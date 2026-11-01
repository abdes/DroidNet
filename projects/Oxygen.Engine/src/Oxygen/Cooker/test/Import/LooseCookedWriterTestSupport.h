//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <utility>

#include <Oxygen/Base/Sha256.h>
#include <Oxygen/Cooker/Cooked/CookerStorage.h>
#include <Oxygen/Cooker/Import/Internal/LooseCookedWriter.h>
#include <Oxygen/Core/Types/AssetKey.h>

namespace oxygen::content::testing {

//! The folder under which the writer tests put their cooked roots in memory.
[[nodiscard]] inline auto MemoryRoot() -> std::filesystem::path
{
  return "/memory";
}

//! Whether `path` names a file or a folder of `storage`.
[[nodiscard]] inline auto Exists(
  const oxygen::content::cooked::CookerStorageReader& storage,
  const std::filesystem::path& path) -> bool
{
  return storage.KindOf(path) != oxygen::content::cooked::PathKind::kNone;
}

//! Asset key whose first byte is `seed` and whose other bytes are zero.
[[nodiscard]] inline auto MakeFirstByteAssetKey(const uint8_t seed)
  -> oxygen::data::AssetKey
{
  auto bytes = std::array<uint8_t, oxygen::data::AssetKey::kSizeBytes> {};
  bytes.at(0) = seed;
  return oxygen::data::AssetKey::FromBytes(bytes);
}

//! The producer of an entry a source cooks.
[[nodiscard]] inline auto FromSource(std::string source)
  -> oxygen::content::import::LooseCookedProducer
{
  return { .source = std::move(source), .completes = {} };
}

//! The digest a producer records for the file it wrote with `bytes`.
[[nodiscard]] inline auto ProducedDigest(const std::span<const std::byte> bytes)
  -> oxygen::content::import::LooseCookedFileDigest
{
  return {
    .size = bytes.size(),
    .sha256 = oxygen::base::ComputeSha256(bytes),
  };
}

} // namespace oxygen::content::testing
