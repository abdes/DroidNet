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

//! Warn about each glTF source feature that model import does not carry into
//! cooked content, including required extensions it does not read. The import
//! continues without them.
OXGN_COOK_API auto ReportDroppedSourceFeatures(const cgltf_data& source,
  std::string_view source_path, std::vector<ImportDiagnostic>& diagnostics)
  -> void;

//! Warn about each FBX source feature that model import does not carry into
//! cooked content, and about source units the import has to assume.
OXGN_COOK_API auto ReportDroppedSourceFeatures(const ufbx_scene& source,
  std::string_view source_path, std::vector<ImportDiagnostic>& diagnostics)
  -> void;

} // namespace oxygen::content::import::internal
