//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>

#include <Oxygen/Vortex/PreparedSceneFrame.h>
#include <Oxygen/Vortex/Renderer.h>
#include <Oxygen/Vortex/SceneRenderer/Stages/DepthPrepass/DepthPrepassMeshProcessor.h>
#include <Oxygen/Vortex/Types/AcceptedDrawView.h>
#include <Oxygen/Vortex/Types/PassMask.h>

namespace oxygen::vortex {

namespace {

auto AppendDrawCommand(std::vector<DrawCommand>& draw_commands,
  const DrawMetadata& metadata, const std::uint32_t draw_index) -> void
{
  draw_commands.push_back(DrawCommand {
    .draw_index = draw_index,
    .index_count
    = metadata.is_indexed != 0U ? metadata.index_count : metadata.vertex_count,
    .instance_count = (std::max)(metadata.instance_count, 1U),
    .start_index = metadata.first_index,
    .base_vertex = metadata.base_vertex,
    .start_instance = 0U,
    .is_indexed = metadata.is_indexed != 0U,
  });
}

} // namespace

DepthPrepassMeshProcessor::DepthPrepassMeshProcessor(Renderer& renderer)
  : renderer_(renderer)
{
}

DepthPrepassMeshProcessor::~DepthPrepassMeshProcessor() = default;

void DepthPrepassMeshProcessor::BuildDrawCommands(
  const PreparedSceneFrame& prepared_scene, const bool include_masked)
{
  (void)renderer_;
  draw_commands_.clear();

  if (prepared_scene.draw_metadata_bytes.empty()) {
    return;
  }

  const auto accept_mask = include_masked
    ? PassMask { PassMaskBit::kOpaque, PassMaskBit::kMasked }
    : PassMask { PassMaskBit::kOpaque };
  const auto accepted_draws = AcceptedDrawView(prepared_scene, accept_mask);
  if (accepted_draws.empty()) {
    return;
  }

  for (const auto [metadata, draw_index] : accepted_draws) {
    AppendDrawCommand(draw_commands_, *metadata, draw_index);
  }
}

auto DepthPrepassMeshProcessor::GetDrawCommands() const
  -> std::span<const DrawCommand>
{
  return draw_commands_;
}

} // namespace oxygen::vortex
