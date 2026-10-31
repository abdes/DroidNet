//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <span>

#include <nlohmann/json.hpp>

#include <Oxygen/Data/PakCatalog.h>

namespace oxygen::content::pak::internal {

//! The JSON of a catalog's dependencies, as the catalog sidecar and the patch
//! manifest write them: by mount, each requirement by kind and key.
[[nodiscard]] auto ToJsonDependencies(
  std::span<const data::PakCatalogDependency> dependencies)
  -> nlohmann::ordered_json;

} // namespace oxygen::content::pak::internal
