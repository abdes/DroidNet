//===----------------------------------------------------------------------===//
// Implementation of AliasLifetimeAnalysis which delegates to
// CreateAsyncEngineResourceValidator and adapts outputs.
//===----------------------------------------------------------------------===//
#include "AliasLifetimeAnalysis.h"
#include "../Integration/GraphicsLayerIntegration.h"

// Forward-declare factory to avoid heavy includes; definition is in
// Resource.cpp
namespace oxygen::engine::asyncsim {
std::unique_ptr<ResourceAliasValidator> CreateAsyncEngineResourceValidator(
  GraphicsLayerIntegration* integration);
}

namespace oxygen::engine::asyncsim {

void AliasLifetimeAnalysis::Initialize(GraphicsLayerIntegration* integration)
{
  validator_ = CreateAsyncEngineResourceValidator(integration);
}

void AliasLifetimeAnalysis::AddResource(ResourceHandle h, const ResourceDesc& d)
{
  if (validator_)
    validator_->AddResource(h, d);
}

void AliasLifetimeAnalysis::AddUsage(ResourceHandle resource, PassHandle pass,
  ResourceState state, bool is_write, ViewIndex view_index)
{
  if (validator_)
    validator_->AddResourceUsage(resource, pass, state, is_write, view_index);
}

void AliasLifetimeAnalysis::SetTopologicalOrder(
  const std::unordered_map<PassHandle, uint32_t>& order)
{
  if (validator_)
    validator_->SetTopologicalOrder(order);
}

void AliasLifetimeAnalysis::AnalyzeLifetimes()
{
  if (validator_)
    validator_->AnalyzeLifetimes();
}

AliasAnalysisResult AliasLifetimeAnalysis::ValidateAndCollect()
{
  AliasAnalysisResult out;
  if (!validator_)
    return out;

  auto hazards = validator_->ValidateAliasing();
  for (const auto& h : hazards) {
    ValidationError e { ValidationErrorType::ResourceAliasHazard,
      h.description };
    e.affected_passes.insert(e.affected_passes.end(),
      h.conflicting_passes.begin(), h.conflicting_passes.end());
    out.hazards.push_back(std::move(e));
  }

  auto candidates = validator_->GetAliasCandidates();
  for (const auto& c : candidates) {
    AliasCandidate ac;
    ac.resource_a = c.resource_a;
    ac.resource_b = c.resource_b;
    ac.combined_memory = c.combined_memory;
    ac.description = c.description;
    out.candidates.push_back(std::move(ac));
  }

  return out;
}

} // namespace oxygen::engine::asyncsim
