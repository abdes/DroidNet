//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <string_view>
#include <vector>

#include <Oxygen/Cooker/Import/ImportDiagnostics.h>
#include <Oxygen/Cooker/api_export.h>

struct cgltf_data;
struct ufbx_scene;

namespace oxygen::content::import::internal {

//! Reject source features that cannot be preserved by static/scalar import.
OXGN_COOK_NDAPI auto ValidateStaticScalarSource(const cgltf_data& source,
  std::string_view source_path, std::vector<ImportDiagnostic>& diagnostics)
  -> bool;

//! Apply the same policy to FBX, including explicit source units and axes.
OXGN_COOK_NDAPI auto ValidateStaticScalarSource(const ufbx_scene& source,
  std::string_view source_path, std::vector<ImportDiagnostic>& diagnostics)
  -> bool;

} // namespace oxygen::content::import::internal
