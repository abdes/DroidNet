//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <algorithm>
#include <string>
#include <string_view>

#include <Oxygen/Base/Sha256.h>
#include <Oxygen/Data/AssetKey.h>

namespace oxygen::content::import::util {

//! Creates a deterministic AssetKey from a virtual path using SHA256.
[[nodiscard]] inline auto MakeDeterministicAssetKey(
  std::string_view virtual_path) -> data::AssetKey
{
  const auto bytes
    = std::as_bytes(std::span(virtual_path.data(), virtual_path.size()));
  const auto digest = base::ComputeSha256(bytes);

  data::AssetKey key {};
  std::copy_n(digest.begin(), key.guid.size(), key.guid.begin());
  return key;
}

//! Creates a random AssetKey.
[[nodiscard]] inline auto MakeRandomAssetKey() -> data::AssetKey
{
  data::AssetKey key {};
  key.guid = data::GenerateAssetGuid();
  return key;
}

} // namespace oxygen::content::import::util
