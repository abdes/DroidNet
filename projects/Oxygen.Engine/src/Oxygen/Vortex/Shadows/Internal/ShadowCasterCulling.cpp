//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Vortex/Shadows/Internal/ShadowCasterCulling.h>

#include <algorithm>

#include <Oxygen/Vortex/Types/PassMask.h>

namespace oxygen::vortex::shadows::internal {

auto ShadowCasterCulling::BuildDrawCommands(
  const PreparedSceneFrame& prepared_scene) -> void
{
  draw_commands_.clear();

  const auto metadata = prepared_scene.GetDrawMetadata();
  draw_commands_.reserve(metadata.size());
  for (std::uint32_t draw_index = 0U; draw_index < metadata.size();
       ++draw_index) {
    const auto& draw = metadata[draw_index];
    if (!draw.flags.IsSet(PassMaskBit::kShadowCaster)) {
      continue;
    }

    draw_commands_.push_back(DrawCommand {
      .draw_index = draw_index,
      .index_count = draw.is_indexed != 0U ? draw.index_count : draw.vertex_count,
      .instance_count = (std::max)(draw.instance_count, 1U),
      .start_index = draw.first_index,
      .base_vertex = draw.base_vertex,
      .start_instance = 0U,
      .is_indexed = draw.is_indexed != 0U,
    });
  }
}

auto ShadowCasterCulling::GetDrawCommands() const -> std::span<const DrawCommand>
{
  return draw_commands_;
}

} // namespace oxygen::vortex::shadows::internal
