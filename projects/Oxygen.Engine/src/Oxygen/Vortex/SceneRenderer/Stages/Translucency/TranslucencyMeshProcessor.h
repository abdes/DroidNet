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
} // namespace oxygen

namespace oxygen::vortex {

struct PreparedSceneFrame;
class Renderer;

struct TranslucencyDrawCommand {
  std::uint32_t draw_index { 0U };
  std::uint32_t material_handle { 0U };
  std::uint32_t geometry_lod_index { 0U };
  std::uint32_t submesh_index { 0U };
  std::uint32_t index_count { 0U };
  std::uint32_t vertex_count { 0U };
  std::uint32_t instance_count { 0U };
  std::uint32_t start_index { 0U };
  std::int32_t base_vertex { 0 };
  std::uint32_t start_instance { 0U };
  bool is_indexed { false };
};

class TranslucencyMeshProcessor {
public:
  OXGN_VRTX_API explicit TranslucencyMeshProcessor(Renderer& renderer);
  OXGN_VRTX_API ~TranslucencyMeshProcessor();

  TranslucencyMeshProcessor(const TranslucencyMeshProcessor&) = delete;
  auto operator=(const TranslucencyMeshProcessor&)
    -> TranslucencyMeshProcessor& = delete;
  TranslucencyMeshProcessor(TranslucencyMeshProcessor&&) = delete;
  auto operator=(TranslucencyMeshProcessor&&)
    -> TranslucencyMeshProcessor& = delete;

  OXGN_VRTX_API void BuildDrawCommands(
    const PreparedSceneFrame& prepared_scene, const ResolvedView* resolved_view);

  [[nodiscard]] OXGN_VRTX_API auto GetDrawCommands() const
    -> std::span<const TranslucencyDrawCommand>;

private:
  Renderer& renderer_;
  std::vector<TranslucencyDrawCommand> draw_commands_ {};
};

} // namespace oxygen::vortex
