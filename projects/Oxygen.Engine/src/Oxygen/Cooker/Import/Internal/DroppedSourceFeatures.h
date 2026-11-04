//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <string_view>
#include <vector>

#include <Oxygen/Cooker/Import/ImportDiagnostics.h>
#include <Oxygen/Cooker/Import/ImportOptions.h>
#include <Oxygen/Cooker/api_export.h>

struct cgltf_data;
struct ufbx_scene;

namespace oxygen::content::import::internal {

//! Warn about each glTF source feature that model import does not carry into
//! cooked content, including required extensions it does not read. The import
//! continues without them.
OXGN_COOK_API auto ReportDroppedSourceFeatures(const cgltf_data& source,
  std::string_view source_path, std::vector<ImportDiagnostic>& diagnostics)
  -> void;

//! Warn about each FBX source feature that model import does not carry into
//! cooked content, and about source units the import has to assume.
//! Warns of an FBX file that states no unit scale, when the import reads it
//! (`fbx.units_unknown`).
OXGN_COOK_API auto ReportFbxUnits(const ufbx_scene& source,
  UnitNormalizationPolicy policy, std::string_view source_path,
  std::vector<ImportDiagnostic>& diagnostics) -> void;

OXGN_COOK_API auto ReportDroppedSourceFeatures(const ufbx_scene& source,
  std::string_view source_path, std::vector<ImportDiagnostic>& diagnostics)
  -> void;

} // namespace oxygen::content::import::internal
