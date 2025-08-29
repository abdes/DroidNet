//===----------------------------------------------------------------------===//
// Strategy wrapper around RenderGraphBuilder::OptimizeSharedPerViewResources
// to allow pluggable registration of the promotion optimization.
//===----------------------------------------------------------------------===//
#pragma once

#include "RenderGraphStrategies.h"

namespace oxygen::engine::asyncsim {

class SharedReadOnlyPromotionStrategy : public IGraphOptimization {
public:
  SharedReadOnlyPromotionStrategy() = default;
  ~SharedReadOnlyPromotionStrategy() override = default;

  void apply(BuildContext& ctx, DiagnosticsSink& /*sink*/) override;
};

} // namespace oxygen::engine::asyncsim
