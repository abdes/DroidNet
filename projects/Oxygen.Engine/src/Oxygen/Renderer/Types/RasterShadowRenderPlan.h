//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstdint>
#include <span>

#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Graphics/Common/Texture.h>
#include <Oxygen/Renderer/Types/DirectionalShadowMetadata.h>
#include <Oxygen/Renderer/Types/ShadowInstanceMetadata.h>
#include <Oxygen/Renderer/Types/ViewConstants.h>

namespace oxygen::renderer {

enum class RasterShadowTargetKind : std::uint32_t {
  kTexture2DArraySlice = 0U,
  kTextureCubeFace = 1U,
};

//! Backend-neutral conventional raster shadow job.
/*!
 The current implementation only emits directional 2D-array slice jobs, but the
 contract is intentionally generic enough for future spot and point raster jobs.
*/
struct RasterShadowJob {
  std::uint32_t shadow_instance_index { 0xFFFFFFFFU };
  std::uint32_t payload_index { 0xFFFFFFFFU };
  std::uint32_t domain { static_cast<std::uint32_t>(
    engine::ShadowDomain::kDirectional) };
  RasterShadowTargetKind target_kind {
    RasterShadowTargetKind::kTexture2DArraySlice
  };
  std::uint32_t target_array_slice { 0U };
  engine::ViewConstants::GpuData view_constants {};
};

//! Per-view conventional raster render plan.
struct RasterShadowRenderPlan {
  observer_ptr<const graphics::Texture> depth_texture;
  std::span<const RasterShadowJob> jobs {};
};

} // namespace oxygen::renderer
