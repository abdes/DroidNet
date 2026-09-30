//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <string>
#include <vector>

#include <Oxygen/Cooker/Import/ImportDiagnostics.h>
#include <Oxygen/Cooker/Import/Internal/MeshTransformBake.h>

namespace oxygen::content::import::adapters {

//! A named geometry variant before vertex/index materialization or slot
//! allocation.
struct ModelGeometrySource final {
  internal::MeshBakeVariant variant;
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

} // namespace oxygen::content::import::adapters
