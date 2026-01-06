//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstdint>
#include <vector>

#include <Oxygen/Content/Import/CookedContentWriter.h>
#include <Oxygen/Content/Import/ImportRequest.h>
#include <Oxygen/Content/Import/ImportedGeometry.h>
#include <Oxygen/Content/Import/fbx/ufbx.h>

namespace oxygen::content::import::emit {

//! Emits a scene descriptor for an FBX scene.
/*!
 The scene descriptor references emitted geometry assets through the
 `geometry` mapping.

 @param scene The loaded FBX scene.
 @param request Import request providing options and output layout.
 @param out Cooked content writer for descriptors and diagnostics.
 @param geometry Mesh-to-geometry-key mapping from geometry emission.
 @param written_scenes Incremented for each emitted scene.
*/
auto WriteSceneAsset(const ufbx_scene& scene, const ImportRequest& request,
  CookedContentWriter& out, const std::vector<ImportedGeometry>& geometry,
  uint32_t& written_scenes) -> void;

} // namespace oxygen::content::import::emit
