//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstddef>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include <Oxygen/Cooker/Import/ImportDiagnostics.h>
#include <Oxygen/Cooker/Import/Internal/MeshTransformBake.h>
#include <Oxygen/Core/Types/AssetKey.h>

namespace oxygen::content::import::adapters {

//! A named geometry variant before vertex/index materialization or surface
//! allocation.
struct ModelGeometrySource final {
  internal::MeshBakeVariant variant;
  //! The variant's index in the bake plan, which scene nodes refer to.
  size_t variant_index = 0;
  std::string name;
  std::string source_id;
};

struct ModelGeometryPreparation final {
  std::vector<ModelGeometrySource> sources;
  std::vector<ImportDiagnostic> diagnostics;
  //! Variant and identity preparation completed; payload validation runs in
  //! cooking.
  bool success = true;
};

//! The geometry key of each bake-plan variant. `keys` holds one key per
//! emitted geometry work item, in the order `emitted_variants` records; a
//! variant whose geometry was dropped has none.
[[nodiscard]] inline auto GeometryKeysByVariant(const size_t variant_count,
  const std::span<const size_t> emitted_variants,
  const std::span<const data::AssetKey> keys)
  -> std::vector<std::optional<data::AssetKey>>
{
  auto by_variant = std::vector<std::optional<data::AssetKey>>(variant_count);
  for (size_t ordinal = 0;
    ordinal < emitted_variants.size() && ordinal < keys.size(); ++ordinal) {
    if (const auto variant = emitted_variants[ordinal];
      variant < by_variant.size()) {
      by_variant[variant] = keys[ordinal];
    }
  }
  return by_variant;
}

} // namespace oxygen::content::import::adapters
