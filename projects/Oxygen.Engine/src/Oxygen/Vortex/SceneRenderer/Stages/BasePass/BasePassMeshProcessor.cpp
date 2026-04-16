//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>

#include <Oxygen/Vortex/PreparedSceneFrame.h>
#include <Oxygen/Vortex/Renderer.h>
#include <Oxygen/Vortex/SceneRenderer/Stages/BasePass/BasePassMeshProcessor.h>
#include <Oxygen/Vortex/Types/AcceptedDrawView.h>
#include <Oxygen/Vortex/Types/PassMask.h>

namespace oxygen::vortex {

namespace {

auto AppendMetadataCommand(std::vector<BasePassDrawCommand>& draw_commands,
  const PreparedSceneFrame& prepared_scene, const DrawMetadata& metadata,
  const std::uint32_t draw_index, const bool write_velocity) -> void
{
  auto material_handle = metadata.material_handle;
  auto geometry_lod_index = 0U;

  if (draw_index < prepared_scene.render_items.size()) {
    const auto& render_item = prepared_scene.render_items[draw_index];
    if (render_item.material_handle.IsValid()) {
      material_handle = render_item.material_handle.get();
    }
    geometry_lod_index = render_item.geometry.IsValid()
      ? render_item.geometry.lod_index
      : render_item.submesh_index;
  }

  draw_commands.push_back(BasePassDrawCommand {
    .draw_index = draw_index,
    .material_handle = material_handle,
    .geometry_lod_index = geometry_lod_index,
    .submesh_index = metadata.submesh_index,
    .index_count = metadata.index_count,
    .vertex_count = metadata.vertex_count,
    .instance_count = (std::max)(metadata.instance_count, 1U),
    .start_index = metadata.first_index,
    .base_vertex = metadata.base_vertex,
    .start_instance = 0U,
    .is_indexed = metadata.is_indexed != 0U,
    .writes_velocity = write_velocity,
  });
}

auto AppendRenderItemCommand(std::vector<BasePassDrawCommand>& draw_commands,
  const PreparedSceneFrame& prepared_scene, const std::uint32_t draw_index,
  const bool write_velocity)
  -> void
{
  const auto& render_item = prepared_scene.render_items[draw_index];
  if (!render_item.main_view_visible) {
    return;
  }

  draw_commands.push_back(BasePassDrawCommand {
    .draw_index = draw_index,
    .material_handle = render_item.material_handle.IsValid()
      ? render_item.material_handle.get()
      : 0U,
    .geometry_lod_index = render_item.geometry.IsValid()
      ? render_item.geometry.lod_index
      : 0U,
    .submesh_index = render_item.submesh_index,
    .instance_count = 1U,
    .is_indexed = render_item.geometry.IsValid(),
    .writes_velocity = write_velocity,
  });
}

auto SortDrawCommands(std::vector<BasePassDrawCommand>& draw_commands) -> void
{
  std::ranges::stable_sort(draw_commands,
    [](const BasePassDrawCommand& lhs,
      const BasePassDrawCommand& rhs) -> bool {
      if (lhs.material_handle != rhs.material_handle) {
        return lhs.material_handle < rhs.material_handle;
      }
      if (lhs.geometry_lod_index != rhs.geometry_lod_index) {
        return lhs.geometry_lod_index < rhs.geometry_lod_index;
      }
      if (lhs.submesh_index != rhs.submesh_index) {
        return lhs.submesh_index < rhs.submesh_index;
      }
      return lhs.draw_index < rhs.draw_index;
    });
}

} // namespace

BasePassMeshProcessor::BasePassMeshProcessor(Renderer& renderer)
  : renderer_(renderer)
{
}

BasePassMeshProcessor::~BasePassMeshProcessor() = default;

void BasePassMeshProcessor::BuildDrawCommands(
  const PreparedSceneFrame& prepared_scene, const ShadingMode mode,
  const bool write_velocity)
{
  (void)renderer_;
  draw_commands_.clear();

  if (mode != ShadingMode::kDeferred) {
    return;
  }

  const auto draw_metadata = prepared_scene.GetDrawMetadata();
  if (!draw_metadata.empty()) {
    const auto accept_mask = PassMask {
      PassMaskBit::kOpaque,
      PassMaskBit::kMasked,
    };
    for (const auto [metadata, draw_index] :
      AcceptedDrawView(prepared_scene, accept_mask)) {
      AppendMetadataCommand(
        draw_commands_, prepared_scene, *metadata, draw_index, write_velocity);
    }
    SortDrawCommands(draw_commands_);
    return;
  }

  for (std::uint32_t draw_index = 0U;
    draw_index < prepared_scene.render_items.size(); ++draw_index) {
    AppendRenderItemCommand(
      draw_commands_, prepared_scene, draw_index, write_velocity);
  }

  SortDrawCommands(draw_commands_);
}

auto BasePassMeshProcessor::GetDrawCommands() const
  -> std::span<const BasePassDrawCommand>
{
  return draw_commands_;
}

} // namespace oxygen::vortex
