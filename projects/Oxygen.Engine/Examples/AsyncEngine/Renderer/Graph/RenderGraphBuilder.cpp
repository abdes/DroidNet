//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include "RenderGraphBuilder.h"
#include "Cache.h"
#include "RenderGraph.h"
#include "Scheduler.h"
#include "Validator.h"

namespace oxygen::examples::asyncsim {

auto RenderGraphBuilder::Build() -> std::unique_ptr<RenderGraph>
{
  // Create the render graph
  auto render_graph = std::make_unique<RenderGraph>();

  // Set frame context and configuration
  render_graph->SetFrameContext(frame_context_);
  render_graph->SetMultiViewEnabled(multi_view_enabled_);

  // Transfer passes to the graph
  for (auto& [handle, pass] : passes_) {
    render_graph->AddPass(handle, std::move(pass));
  }

  // Transfer resource descriptors
  for (auto& [handle, desc] : resource_descriptors_) {
    render_graph->AddResourceDescriptor(handle, std::move(desc));
  }

  // Validate the graph (Phase 1 - basic validation)
  auto validator = std::make_unique<RenderGraphValidator>();
  auto validation_result = validator->ValidateGraph(*this);
  render_graph->SetValidationResult(validation_result);

  // Schedule passes (Phase 1 - basic scheduling)
  auto scheduler = std::make_unique<RenderGraphScheduler>();
  auto scheduling_result = scheduler->SchedulePasses(*render_graph);
  render_graph->SetSchedulingResult(scheduling_result);
  render_graph->SetExecutionOrder(scheduling_result.execution_order);

  // Generate cache key
  RenderGraphCacheKey cache_key;
  cache_key.view_count = static_cast<uint32_t>(frame_context_.views.size());
  cache_key.structure_hash
    = cache_utils::ComputeStructureHash(render_graph->GetPassHandles());
  cache_key.resource_hash
    = cache_utils::ComputeResourceHash(render_graph->GetResourceHandles());
  cache_key.viewport_hash
    = cache_utils::ComputeViewportHash(frame_context_.views);
  render_graph->SetCacheKey(cache_key);

  return render_graph;
}

} // namespace oxygen::examples::asyncsim
