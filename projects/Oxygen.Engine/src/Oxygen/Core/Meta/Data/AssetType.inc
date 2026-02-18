//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//
//
// Shared AssetType ID catalog.
//
// Expected consumer macro:
//   OXDAT_ASSET_TYPE(name, value)
//
// Macro is optional; undefined expands to nothing.

#ifndef OXDAT_ASSET_TYPE
#  define OXDAT_ASSET_TYPE(name, value)
#endif

OXDAT_ASSET_TYPE(kUnknown, 0)
OXDAT_ASSET_TYPE(kMaterial, 1)
OXDAT_ASSET_TYPE(kGeometry, 2)
OXDAT_ASSET_TYPE(kScene, 3)
OXDAT_ASSET_TYPE(kScript, 4)
OXDAT_ASSET_TYPE(kMaxAssetType, kScript)

#undef OXDAT_ASSET_TYPE
