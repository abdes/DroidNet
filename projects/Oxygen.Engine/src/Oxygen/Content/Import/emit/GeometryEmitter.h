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
#include <Oxygen/Data/AssetKey.h>

namespace oxygen::content::import::emit {

//! Emits geometry assets and buffer resources for an FBX scene.
/*!
 Emits:

 - Geometry asset descriptors per mesh.
 - Buffer resources for vertex and index data.
 - Buffer table file and registers the external buffer data file.

 @param scene The loaded FBX scene.
 @param request Import request providing options and output layout.
 @param out Cooked content writer for descriptors and diagnostics.
 @param material_keys Material keys aligned with scene materials.
 @param out_geometry Accumulates mesh-to-asset-key mappings.
 @param written_geometry Incremented for each emitted geometry.
 @param want_textures Whether textures are being imported (for diagnostics).
*/
auto WriteGeometryAssets(const ufbx_scene& scene, const ImportRequest& request,
  CookedContentWriter& out,
  const std::vector<oxygen::data::AssetKey>& material_keys,
  std::vector<ImportedGeometry>& out_geometry, uint32_t& written_geometry,
  bool want_textures) -> void;

} // namespace oxygen::content::import::emit
