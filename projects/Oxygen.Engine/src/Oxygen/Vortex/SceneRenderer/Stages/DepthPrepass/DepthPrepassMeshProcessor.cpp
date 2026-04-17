//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <cmath>
#include <limits>
#include <vector>

#include <Oxygen/Core/Types/ResolvedView.h>
#include <Oxygen/Vortex/PreparedSceneFrame.h>
#include <Oxygen/Vortex/Renderer.h>
#include <Oxygen/Vortex/SceneRenderer/Stages/DepthPrepass/DepthPrepassMeshProcessor.h>
#include <Oxygen/Vortex/Types/AcceptedDrawView.h>
#include <Oxygen/Vortex/Types/PassMask.h>

namespace oxygen::vortex {

namespace {

struct SortableDrawCommand {
  DrawCommand draw_command {};
  float front_to_back_key { 0.0F };
};

auto MakeDrawCommand(const DrawMetadata& metadata, const std::uint32_t draw_index)
  -> DrawCommand
{
  return DrawCommand {
    .draw_index = draw_index,
    .index_count
    = metadata.is_indexed != 0U ? metadata.index_count : metadata.vertex_count,
    .instance_count = (std::max)(metadata.instance_count, 1U),
    .start_index = metadata.first_index,
    .base_vertex = metadata.base_vertex,
    .start_instance = 0U,
    .is_indexed = metadata.is_indexed != 0U,
  };
}

auto IsPerspectiveProjection(const ResolvedView& view) -> bool
{
  return std::abs(view.ProjectionMatrix()[2][3]) > 0.5F;
}

auto ComputeFrontToBackKey(const PreparedSceneFrame& prepared_scene,
  const ResolvedView* resolved_view, const std::uint32_t draw_index) -> float
{
  if (resolved_view == nullptr
    || draw_index >= prepared_scene.draw_bounding_spheres.size()) {
    return 0.0F;
  }

  const auto sphere = prepared_scene.draw_bounding_spheres[draw_index];
  const auto view_space_center = resolved_view->ViewMatrix()
    * glm::vec4(sphere.x, sphere.y, sphere.z, 1.0F);
  const auto forward_depth = (std::max)(-view_space_center.z, 0.0F);
  if (!std::isfinite(forward_depth)) {
    return (std::numeric_limits<float>::max)();
  }

  if (IsPerspectiveProjection(*resolved_view)) {
    const auto radius = (std::max)(sphere.w, 0.0F);
    return (std::max)(forward_depth - radius, 0.0F);
  }

  return forward_depth;
}

auto StableSortFrontToBack(
  std::vector<SortableDrawCommand>& draw_commands) -> void
{
  std::ranges::stable_sort(draw_commands,
    [](const SortableDrawCommand& lhs, const SortableDrawCommand& rhs) -> bool {
      return lhs.front_to_back_key < rhs.front_to_back_key;
    });
}

auto AppendCommands(std::vector<DrawCommand>& out,
  const std::vector<SortableDrawCommand>& in) -> void
{
  for (const auto& command : in) {
    out.push_back(command.draw_command);
  }
}

} // namespace

DepthPrepassMeshProcessor::DepthPrepassMeshProcessor(Renderer& renderer)
  : renderer_(renderer)
{
}

DepthPrepassMeshProcessor::~DepthPrepassMeshProcessor() = default;

void DepthPrepassMeshProcessor::BuildDrawCommands(
  const PreparedSceneFrame& prepared_scene, const ResolvedView* resolved_view,
  const bool include_masked)
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

  auto opaque_draws = std::vector<SortableDrawCommand> {};
  auto masked_draws = std::vector<SortableDrawCommand> {};
  for (const auto [metadata, draw_index] : accepted_draws) {
    auto sortable = SortableDrawCommand {
      .draw_command = MakeDrawCommand(*metadata, draw_index),
      .front_to_back_key
      = ComputeFrontToBackKey(prepared_scene, resolved_view, draw_index),
    };
    if (metadata->flags.IsSet(PassMaskBit::kMasked)) {
      masked_draws.push_back(std::move(sortable));
    } else {
      opaque_draws.push_back(std::move(sortable));
    }
  }

  StableSortFrontToBack(opaque_draws);
  StableSortFrontToBack(masked_draws);

  draw_commands_.reserve(opaque_draws.size() + masked_draws.size());
  AppendCommands(draw_commands_, opaque_draws);
  AppendCommands(draw_commands_, masked_draws);
}

auto DepthPrepassMeshProcessor::GetDrawCommands() const
  -> std::span<const DrawCommand>
{
  return draw_commands_;
}

} // namespace oxygen::vortex
