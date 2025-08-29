//===----------------------------------------------------------------------===//
// Per-view expansion service: encapsulates per-view resource and pass
// cloning and active view determination.
//===----------------------------------------------------------------------===//
#pragma once

#include <optional>
#include <vector>

#include "../../Types/ViewIndex.h"

namespace oxygen::engine::asyncsim {

class RenderGraphBuilder;
class RenderGraph;

class PerViewExpansionService {
public:
  explicit PerViewExpansionService(RenderGraphBuilder& builder)
    : builder_(builder)
  {
  }

  // Determine active views according to the builder configuration
  auto DetermineActiveViews() -> std::vector<ViewIndex>;

  // Expand per-view resources (delegates to builder helpers)
  auto ExpandPerViewResources(RenderGraph* render_graph) -> void;

private:
  RenderGraphBuilder& builder_;
};

} // namespace oxygen::engine::asyncsim
