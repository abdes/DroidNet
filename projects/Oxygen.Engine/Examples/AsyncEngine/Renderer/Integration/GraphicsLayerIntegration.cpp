//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include "GraphicsLayerIntegration.h"

#include <Oxygen/Base/Logging.h>

namespace oxygen::engine::asyncsim {

auto GraphicsLayerIntegration::RegisterResource(
  const std::string& resource_name) -> ResourceHandle
{
  LOG_F(3, "[GraphicsIntegration] Registering resource: {}", resource_name);

  // Register with the global resource registry
  const auto engine_handle
    = graphics_layer_.GetResourceRegistry().RegisterResource(resource_name);

  ++total_resources_registered_;

  const auto render_graph_handle = ConvertToRenderGraphHandle(engine_handle);

  LOG_F(3, "[GraphicsIntegration] Resource '{}' registered with handle {}",
    resource_name, render_graph_handle.get());

  return render_graph_handle;
}

auto GraphicsLayerIntegration::UnregisterResource(
  ResourceHandle resource_handle, uint64_t frame_index,
  const std::string& debug_name) -> void
{
  LOG_F(3, "[GraphicsIntegration] Unregistering resource '{}' (handle: {})",
    debug_name, resource_handle.get());

  // Schedule for deferred cleanup through the graphics layer
  ScheduleResourceReclaim(resource_handle, frame_index, debug_name);
}

auto GraphicsLayerIntegration::AllocateDescriptor()
  -> RenderGraphDescriptorIndex
{
  LOG_F(3, "[GraphicsIntegration] Allocating descriptor");

  // Allocate descriptor using the global descriptor allocator
  const auto engine_descriptor
    = graphics_layer_.GetDescriptorAllocator().AllocateDescriptor();

  ++total_descriptors_allocated_;

  const auto render_graph_descriptor
    = ConvertToRenderGraphDescriptor(engine_descriptor);

  LOG_F(3, "[GraphicsIntegration] Descriptor allocated: {}",
    render_graph_descriptor.get());

  return render_graph_descriptor;
}

auto GraphicsLayerIntegration::PublishDescriptorTable(uint64_t version) -> void
{
  LOG_F(3, "[GraphicsIntegration] Publishing descriptor table (version: {})",
    version);

  // Publish descriptor table changes atomically
  graphics_layer_.GetDescriptorAllocator().PublishDescriptorTable(version);
}

auto GraphicsLayerIntegration::ScheduleResourceReclaim(
  ResourceHandle resource_handle, uint64_t submitted_frame,
  const std::string& debug_name) -> void
{
  LOG_F(3, "[GraphicsIntegration] Scheduling reclaim for '{}' (frame: {})",
    debug_name, submitted_frame);

  // Schedule reclamation through the deferred reclaimer
  graphics_layer_.GetDeferredReclaimer().ScheduleReclaim(
    resource_handle.get(), submitted_frame, debug_name);
}

auto GraphicsLayerIntegration::GetPendingReclaimCount() const -> std::size_t
{
  return graphics_layer_.GetDeferredReclaimer().GetPendingCount();
}

auto GraphicsLayerIntegration::ProcessCompletedFrames() -> std::size_t
{
  LOG_F(3, "[GraphicsIntegration] Processing completed frames");

  return graphics_layer_.ProcessCompletedFrames();
}

auto GraphicsLayerIntegration::GetIntegrationStats() const -> IntegrationStats
{
  IntegrationStats stats;
  stats.active_resources = total_resources_registered_;
  stats.allocated_descriptors = total_descriptors_allocated_;
  stats.pending_reclaims = GetPendingReclaimCount();
  stats.last_reclaimed_count = graphics_layer_.GetLastReclaimedCount();

  return stats;
}

auto GraphicsLayerIntegration::ValidateIntegrationState() const -> bool
{
  // Basic consistency checks
  const auto stats = GetIntegrationStats();

  if (stats.pending_reclaims > stats.active_resources) {
    LOG_F(WARNING,
      "[GraphicsIntegration] Invalid state: more pending reclaims ({}) "
      "than active resources ({})",
      stats.pending_reclaims, stats.active_resources);
    return false;
  }

  LOG_F(3,
    "[GraphicsIntegration] Integration state valid - "
    "Resources: {}, Descriptors: {}, Pending: {}",
    stats.active_resources, stats.allocated_descriptors,
    stats.pending_reclaims);

  return true;
}

} // namespace oxygen::engine::asyncsim
