//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <Oxygen/Graphics/Common/Texture.h>
#include <Oxygen/Vortex/Internal/MeshRasterState.h>

namespace oxygen::vortex::internal {

//! Opaque/masked depth prepass pipeline; reads the bound camera view.
[[nodiscard]] auto BuildMeshDepthPipeline(const graphics::TextureDesc& depth,
  Format velocity_format, MeshRasterState raster_state, bool reverse_z)
  -> graphics::GraphicsPipelineDesc;

//! Opaque/masked contact-shadow caster depth pipeline; reads only its pass
//! inputs record, never the bound view.
[[nodiscard]] auto BuildContactShadowCasterDepthPipeline(
  const graphics::TextureDesc& depth, MeshRasterState raster_state,
  bool reverse_z) -> graphics::GraphicsPipelineDesc;

} // namespace oxygen::vortex::internal
