//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <string>
#include <vector>

#include <Oxygen/Cooker/Import/ImportDiagnostics.h>
#include <Oxygen/Cooker/Import/Internal/MaterialSource.h>

namespace oxygen::content::import::adapters {

//! Named material recipe retaining its parsed model's source identity.
struct ModelMaterialSource final {
  std::string source_id;
  const void* source_key = nullptr;
  MaterialSource material;
};

//! Preparation assigns names once; consumers reuse these owned recipes.
//! Parser-owned source_key values remain valid while the adapter is alive.
struct ModelMaterialPreparation final {
  std::vector<ModelMaterialSource> sources;
  std::vector<ImportDiagnostic> diagnostics;
  bool success = true;
};

} // namespace oxygen::content::import::adapters
