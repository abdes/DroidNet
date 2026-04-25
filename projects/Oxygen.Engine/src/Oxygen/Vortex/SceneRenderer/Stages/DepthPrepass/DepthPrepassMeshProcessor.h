//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstdint>
#include <span>
#include <vector>

#include <Oxygen/Vortex/api_export.h>

namespace oxygen {
class ResolvedView;
}

namespace oxygen::vortex {

struct PreparedSceneFrame;
class Renderer;

struct DrawCommand {
  std::uint32_t draw_index { 0U };
  std::uint32_t index_count { 0U };
  std::uint32_t instance_count { 0U };
  std::uint32_t start_index { 0U };
  std::int32_t base_vertex { 0 };
  std::uint32_t start_instance { 0U };
  bool is_indexed { false };
};

class DepthPrepassMeshProcessor {
public:
  OXGN_VRTX_API explicit DepthPrepassMeshProcessor(Renderer& renderer);
  OXGN_VRTX_API ~DepthPrepassMeshProcessor();

  DepthPrepassMeshProcessor(const DepthPrepassMeshProcessor&) = delete;
  auto operator=(const DepthPrepassMeshProcessor&)
    -> DepthPrepassMeshProcessor& = delete;
  DepthPrepassMeshProcessor(DepthPrepassMeshProcessor&&) = delete;
  auto operator=(DepthPrepassMeshProcessor&&)
    -> DepthPrepassMeshProcessor& = delete;

  OXGN_VRTX_API void BuildDrawCommands(
    const PreparedSceneFrame& prepared_scene,
    const ResolvedView* resolved_view, bool include_masked);

  [[nodiscard]] OXGN_VRTX_API auto GetDrawCommands() const
    -> std::span<const DrawCommand>;

private:
  Renderer& renderer_;
  std::vector<DrawCommand> draw_commands_ {};
};

} // namespace oxygen::vortex
