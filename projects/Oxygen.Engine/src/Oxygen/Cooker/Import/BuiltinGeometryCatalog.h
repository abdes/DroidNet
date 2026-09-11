//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <string>
#include <string_view>

#include <Oxygen/Cooker/api_export.h>

namespace oxygen::content::import {

//! Exports built-in identities and schema-valid geometry/material
//! contributions. The mount is project policy; recipes, bounds and materials
//! come from Oxygen.Data. Throws std::invalid_argument for an invalid mount, or
//! std::runtime_error if an advertised generator cannot produce its default
//! geometry.
OXGN_COOK_NDAPI auto ExportBuiltinGeometryCatalog(std::string_view mount_name)
  -> std::string;

} // namespace oxygen::content::import
