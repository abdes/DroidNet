//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <array>
#include <filesystem>

#include <Oxygen/Data/AssetKey.h>
#include <Oxygen/Data/SourceKey.h>

namespace oxygen::content::testing {

auto WriteMaterialSource(const std::filesystem::path& root,
  const data::AssetKey& key, const std::array<float, 4>& color)
  -> data::SourceKey;

//! A material with two slots sharing one texture; exercises dependency
//! deduplication.
auto WriteTexturedMaterialSource(const std::filesystem::path& root,
  const data::AssetKey& key) -> data::SourceKey;

auto WriteScriptSource(const std::filesystem::path& root,
  const data::AssetKey& key) -> data::SourceKey;

} // namespace oxygen::content::testing
