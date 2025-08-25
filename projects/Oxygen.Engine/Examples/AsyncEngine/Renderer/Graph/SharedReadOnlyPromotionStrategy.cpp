//===----------------------------------------------------------------------===//
// Implementation of SharedReadOnlyPromotionStrategy
//===----------------------------------------------------------------------===//
#include "SharedReadOnlyPromotionStrategy.h"
#include "RenderGraphBuilder.h"

namespace oxygen::examples::asyncsim {

void SharedReadOnlyPromotionStrategy::apply(
  BuildContext& ctx, DiagnosticsSink& /*sink*/)
{
  if (!ctx.builder || !ctx.render_graph)
    return;
  // Delegate to existing builder implementation for now
  ctx.builder->RunOptimizeSharedPerViewResources(ctx.render_graph);
}

} // namespace oxygen::examples::asyncsim
