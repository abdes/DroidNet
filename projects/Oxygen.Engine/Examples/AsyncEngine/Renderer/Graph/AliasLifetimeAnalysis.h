//===----------------------------------------------------------------------===//
// Alias lifetime analysis wrapper providing a compact value-style API used by
// RenderGraphBuilder. This wraps the integration-specific
// ResourceAliasValidator while exposing simple inputs/outputs for easier
// testing and strategy wiring.
//===----------------------------------------------------------------------===//
#pragma once

#include <memory>
#include <unordered_map>
#include <vector>

#include "../../Types/ViewIndex.h"
#include "Resource.h"
#include "Types.h"
#include "Validator.h"

namespace oxygen::examples::asyncsim {

// Simple value-like outputs from lifetime analysis
struct AliasAnalysisResult {
  std::vector<ValidationError> hazards;
  std::vector<AliasCandidate> candidates; // uses type from Resource.h
};

class AliasLifetimeAnalysis {
public:
  AliasLifetimeAnalysis() = default;
  ~AliasLifetimeAnalysis() = default;

  // Configure with integration helpers (graphics layer)
  void Initialize(GraphicsLayerIntegration* integration);

  // Register resources and usages (delegates to validator)
  void AddResource(ResourceHandle h, const ResourceDesc& desc);
  void AddUsage(ResourceHandle resource, PassHandle pass, ResourceState state,
    bool is_write, ViewIndex view_index = ViewIndex { 0 });

  // Set topological order mapping and run lifetime analysis
  void SetTopologicalOrder(
    const std::unordered_map<PassHandle, uint32_t>& order);
  void AnalyzeLifetimes();

  // Validate aliasing and return structured result
  AliasAnalysisResult ValidateAndCollect();

private:
  std::unique_ptr<ResourceAliasValidator> validator_;
};

} // namespace oxygen::examples::asyncsim
