//===----------------------------------------------------------------------===//
// Implementation of per-view expansion service.
//===----------------------------------------------------------------------===//

#include "PerViewExpansionService.h"
#include "RenderGraph.h"
#include "RenderGraphBuilder.h"

#include <algorithm>

namespace oxygen::engine::asyncsim {

auto PerViewExpansionService::DetermineActiveViews() -> std::vector<ViewIndex>
{
  // Reuse builder logic: call builder's DetermineActiveViews via a small
  // friend-style access by invoking the same semantics (we'll call the
  // builder's method indirectly through a lambda if necessary). For now we
  // directly call the builder's DetermineActiveViews() method.
  return builder_.RunDetermineActiveViews();
}

auto PerViewExpansionService::ExpandPerViewResources(
  RenderGraph* /*render_graph*/) -> void
{
  // Collect handles that require per-view expansion and delegate to the
  // builder's CreatePerViewResources for each.
  std::vector<ResourceHandle> originals;
  originals.reserve(builder_.GetResourceDescriptors().size());

  for (const auto& [handle, desc] : builder_.GetResourceDescriptors()) {
    if (!desc)
      continue;
    if (desc->GetScope() == ResourceScope::PerView) {
      originals.push_back(handle);
    }
  }

  for (auto const h : originals) {
    auto const* desc = builder_.GetResourceDescriptor(h);
    if (!desc)
      continue;
    builder_.RunCreatePerViewResources(h, *desc);
  }
}

} // namespace oxygen::engine::asyncsim
