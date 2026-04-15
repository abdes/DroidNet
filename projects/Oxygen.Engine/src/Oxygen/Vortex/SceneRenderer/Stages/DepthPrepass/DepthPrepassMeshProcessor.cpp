//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>

#include <Oxygen/Vortex/PreparedSceneFrame.h>
#include <Oxygen/Vortex/Renderer.h>
#include <Oxygen/Vortex/SceneRenderer/Stages/DepthPrepass/DepthPrepassMeshProcessor.h>
#include <Oxygen/Vortex/Types/DrawMetadata.h>
#include <Oxygen/Vortex/Types/PassMask.h>

namespace oxygen::vortex {

namespace {

auto ShouldIncludePass(
  const PassMask mask, const bool include_masked) noexcept -> bool
{
  return mask.IsSet(PassMaskBit::kOpaque)
    || (include_masked && mask.IsSet(PassMaskBit::kMasked));
}

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

  const auto draw_count
    = prepared_scene.draw_metadata_bytes.size() / sizeof(DrawMetadata);
  if (draw_count == 0U) {
    return;
  }

  // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
  const auto* draw_records = reinterpret_cast<const DrawMetadata*>(
    prepared_scene.draw_metadata_bytes.data());

  if (!prepared_scene.partitions.empty()) {
    for (const auto& partition : prepared_scene.partitions) {
      if (!ShouldIncludePass(partition.pass_mask, include_masked)) {
        continue;
      }

      const auto begin = (std::min)(partition.begin, static_cast<std::uint32_t>(draw_count));
      const auto end = (std::min)(partition.end, static_cast<std::uint32_t>(draw_count));
      for (auto draw_index = begin; draw_index < end; ++draw_index) {
        // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-pointer-arithmetic)
        AppendDrawCommand(draw_commands_, draw_records[draw_index], draw_index);
      }
    }
    return;
  }

  for (std::uint32_t draw_index = 0U; draw_index < draw_count; ++draw_index) {
    // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-pointer-arithmetic)
    const auto& metadata = draw_records[draw_index];
    if (!ShouldIncludePass(metadata.flags, include_masked)) {
      continue;
    }
    AppendDrawCommand(draw_commands_, metadata, draw_index);
  }
}

auto DepthPrepassMeshProcessor::GetDrawCommands() const
  -> std::span<const DrawCommand>
{
  return draw_commands_;
}

} // namespace oxygen::vortex
