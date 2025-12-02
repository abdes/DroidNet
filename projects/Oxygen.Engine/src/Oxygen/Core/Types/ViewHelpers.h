//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <glm/glm.hpp>

#include <Oxygen/Core/Types/ResolvedView.h>
#include <Oxygen/Core/Types/ViewPort.h>

namespace oxygen {

// Convert pixel jitter (pixels, +X right, +Y down) into NDC offsets for
// column-major GLM matrices. Output NDC values are in [-2,2] range scale
// such that a 1-pixel shift yields ndc = 2/width (per-axis).
inline glm::vec2 PixelJitterToNdc(
  const glm::vec2& pixel_jitter_px, const ViewPort& vp)
{
  const float w = std::max(1.0f, static_cast<float>(vp.width));
  const float h = std::max(1.0f, static_cast<float>(vp.height));
  const float ndc_x = (2.0f * pixel_jitter_px.x) / w;
  const float ndc_y = (2.0f * pixel_jitter_px.y) / h;
  // Pixel Y is defined positive-down; NDC Y is positive-up, so invert Y here.
  return glm::vec2(ndc_x, -ndc_y);
}

// Apply a clip-space translation (jitter) to the projection matrix.
// For GLM column-major matrices, place offsets in column 3 rows 0/1 and
// left-multiply to apply translation in clip-space: proj' = jitterMat * proj.
inline glm::mat4 ApplyJitterToProjection(
  const glm::mat4& proj, const glm::vec2& pixel_jitter_px, const ViewPort& vp)
{
  if (pixel_jitter_px == glm::vec2(0.0f))
    return proj;
  const glm::vec2 ndc = PixelJitterToNdc(pixel_jitter_px, vp);
  glm::mat4 jitter(1.0f);
  jitter[3][0] = ndc.x;
  jitter[3][1] = ndc.y;
  return jitter * proj;
}

// Remap projection depth-range between GL [-1,1] and D3D [0,1]. Left-multiply
// remap matrix: proj_out = remap * proj_in.
inline glm::mat4 RemapProjectionDepthRange(
  const glm::mat4& proj, NdcDepthRange from, NdcDepthRange to)
{
  if (from == to)
    return proj;
  glm::mat4 remap(1.0f);
  if (from == NdcDepthRange::MinusOneToOne && to == NdcDepthRange::ZeroToOne) {
    remap[2][2] = 0.5f;
    remap[3][2] = 0.5f;
    return remap * proj;
  }
  if (from == NdcDepthRange::ZeroToOne && to == NdcDepthRange::MinusOneToOne) {
    remap[2][2] = 2.0f;
    remap[3][2] = -1.0f;
    return remap * proj;
  }
  return proj;
}

} // namespace oxygen
