//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <Oxygen/Vortex/Internal/MeshRasterState.h>
#include <Oxygen/Graphics/Common/Texture.h>

namespace oxygen::vortex::internal {

//! Shared opaque/masked depth pipeline for camera and contact-caster depth.
[[nodiscard]] auto BuildMeshDepthPipeline(const graphics::TextureDesc& depth,
  Format velocity_format, MeshRasterState raster_state, bool reverse_z)
  -> graphics::GraphicsPipelineDesc;

} // namespace oxygen::vortex::internal
