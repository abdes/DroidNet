//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <Oxygen/Content/Import/fbx/ufbx.h>
#include <Oxygen/Data/AssetKey.h>

namespace oxygen::content::import {

//! Maps a source mesh pointer to an emitted geometry asset key.
struct ImportedGeometry final {
  const ufbx_mesh* mesh = nullptr;
  oxygen::data::AssetKey key = {};
};

} // namespace oxygen::content::import
