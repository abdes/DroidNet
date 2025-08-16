//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstdint>
#include <memory>
#include <span>
#include <vector>

#include <Oxygen/Base/Macros.h>
#include <Oxygen/Renderer/Extraction/RenderItemData.h>
#include <Oxygen/Renderer/api_export.h>

namespace oxygen::scene {
class Scene;
}
namespace oxygen::engine {
class View;
struct RenderContext;
class RenderItemsList;
}

namespace oxygen::engine::extraction {

//! Stateful, minimal render list builder (PIMPL).
/*!
 Minimal PIMPL-backed builder used to collect and finalize render items.

 This header provides a small, no-op implementation surface so that the
 rest of the renderer can call into a stable API while the Collect/Finalize
 implementations are developed incrementally.
*/
class RenderListBuilder {
public:
  //! Construct builder with default configuration
  OXGN_RNDR_API RenderListBuilder();

  OXYGEN_MAKE_NON_COPYABLE(RenderListBuilder)
  OXYGEN_DEFAULT_MOVABLE(RenderListBuilder)

  OXGN_RNDR_API ~RenderListBuilder();

  //! Phase 1: Collect render items from scene (CPU-only, no GPU work)
  OXGN_RNDR_NDAPI auto Collect(scene::Scene& scene, const View& view,
    std::uint64_t frame_id) -> std::vector<RenderItemData>;

  //! Phase 2: Finalize items into GPU-ready renderable list
  OXGN_RNDR_API auto Finalize(std::span<const RenderItemData> collected_items,
    RenderContext& render_context, RenderItemsList& output) -> void;

  //! Clean up stale resources that haven't been used recently
  OXGN_RNDR_API auto EvictStaleResources(RenderContext& render_context,
    std::uint64_t current_frame_id, std::uint32_t keep_frame_count = 3) -> void;

private:
  class Impl;
  std::unique_ptr<Impl> impl_;
};

} // namespace oxygen::engine::extraction
