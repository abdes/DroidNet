//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstdint>
#include <span>

#include <Oxygen/Graphics/Common/PipelineState.h>
#include <Oxygen/Vortex/Types/DrawMetadata.h>

namespace oxygen::vortex::internal {

//! Per-draw mesh pipeline state shared by color, depth, velocity, and shadows.
struct MeshRasterState {
  bool alpha_test { false };
  bool double_sided { false };
  bool reverse_winding { false };

  auto operator==(const MeshRasterState&) const -> bool = default;

  [[nodiscard]] auto Rasterizer() const -> graphics::RasterizerStateDesc
  {
    return {
      .cull_mode
      = double_sided ? graphics::CullMode::kNone : graphics::CullMode::kBack,
      // Even without culling, SV_IsFrontFace must describe the authored
      // exterior so double-sided lighting reverses the correct surface normal.
      .front_counter_clockwise = !reverse_winding,
    };
  }
};

[[nodiscard]] inline auto ResolveMeshRasterState(
  const std::span<const DrawMetadata> metadata, const std::uint32_t draw_index)
  -> MeshRasterState
{
  if (draw_index >= metadata.size()) {
    return {};
  }
  const auto flags = metadata[draw_index].flags;
  return {
    .alpha_test = flags.IsSet(PassMaskBit::kMasked),
    .double_sided = flags.IsSet(PassMaskBit::kDoubleSided),
    .reverse_winding = flags.IsSet(PassMaskBit::kReverseWinding),
  };
}

} // namespace oxygen::vortex::internal
