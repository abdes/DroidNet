//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstdint>
#include <string>

#include <Oxygen/Base/NamedType.h>

#include "../../GraphicsLayer.h"

namespace oxygen::examples::asyncsim {

//! Strong types for render graph resource integration
namespace {
  // clang-format off
  using RenderGraphResourceHandle = oxygen::NamedType<uint64_t, struct RenderGraphResourceHandleTag,
    oxygen::Hashable,
    oxygen::Comparable,
    oxygen::Printable>;

  using RenderGraphDescriptorIndex = oxygen::NamedType<uint32_t, struct RenderGraphDescriptorIndexTag,
    oxygen::DefaultInitialized,
    oxygen::Comparable,
    oxygen::Printable,
    oxygen::Hashable>;
  // clang-format on
}

//! Integration layer bridging render graph resources with AsyncEngine
//! GraphicsLayer
/*!
 Provides a clean interface for render graph resources to integrate with:
 - GlobalResourceRegistry for bindless resource access
 - GlobalDescriptorAllocator for descriptor heap management
 - DeferredReclaimer for safe resource lifetime management

 This layer handles the translation between render graph abstractions
 and the concrete AsyncEngine graphics infrastructure.
 */
class GraphicsLayerIntegration {
public:
  explicit GraphicsLayerIntegration(GraphicsLayer& graphics_layer) noexcept
    : graphics_layer_(graphics_layer)
  {
  }

  // Non-copyable, movable
  GraphicsLayerIntegration(const GraphicsLayerIntegration&) = delete;
  auto operator=(const GraphicsLayerIntegration&)
    -> GraphicsLayerIntegration& = delete;
  GraphicsLayerIntegration(GraphicsLayerIntegration&&) = default;
  auto operator=(GraphicsLayerIntegration&&)
    -> GraphicsLayerIntegration& = default;

  // === RESOURCE REGISTRATION ===

  //! Register a render graph resource with the global resource registry
  [[nodiscard]] auto RegisterResource(const std::string& resource_name)
    -> RenderGraphResourceHandle;

  //! Unregister a render graph resource (schedules deferred cleanup)
  auto UnregisterResource(RenderGraphResourceHandle resource_handle,
    uint64_t frame_index, const std::string& debug_name) -> void;

  // === DESCRIPTOR ALLOCATION ===

  //! Allocate a descriptor for bindless access
  [[nodiscard]] auto AllocateDescriptor() -> RenderGraphDescriptorIndex;

  //! Publish descriptor table changes (atomic publication)
  auto PublishDescriptorTable(uint64_t version) -> void;

  // === RESOURCE LIFETIME MANAGEMENT ===

  //! Schedule resource for deferred reclamation
  auto ScheduleResourceReclaim(RenderGraphResourceHandle resource_handle,
    uint64_t submitted_frame, const std::string& debug_name) -> void;

  //! Get count of pending reclaim operations
  [[nodiscard]] auto GetPendingReclaimCount() const -> std::size_t;

  //! Process completed frames and trigger resource reclamation
  [[nodiscard]] auto ProcessCompletedFrames() -> std::size_t;

  // === FRAME LIFECYCLE ===

  //! Begin frame processing (triggers cleanup of old resources)
  auto BeginFrame(uint64_t frame_index) -> void;

  //! End frame processing
  auto EndFrame() -> void;

  // === DEBUGGING AND DIAGNOSTICS ===

  //! Get integration statistics for debugging
  struct IntegrationStats {
    std::size_t active_resources { 0 };
    std::size_t allocated_descriptors { 0 };
    std::size_t pending_reclaims { 0 };
    std::size_t last_reclaimed_count { 0 };
  };

  [[nodiscard]] auto GetIntegrationStats() const -> IntegrationStats;

  //! Validate integration state consistency
  [[nodiscard]] auto ValidateIntegrationState() const -> bool;

private:
  // === INTERNAL STATE ===

  GraphicsLayer& graphics_layer_;

  // Statistics and debugging
  mutable std::size_t total_resources_registered_ { 0 };
  mutable std::size_t total_descriptors_allocated_ { 0 };

  // === INTERNAL HELPERS ===

  //! Convert engine resource handle to render graph handle
  [[nodiscard]] auto ConvertToRenderGraphHandle(uint64_t engine_handle) const
    -> RenderGraphResourceHandle
  {
    return RenderGraphResourceHandle { engine_handle };
  }

  //! Convert engine descriptor ID to render graph descriptor index
  [[nodiscard]] auto ConvertToRenderGraphDescriptor(
    uint32_t engine_descriptor) const -> RenderGraphDescriptorIndex
  {
    return RenderGraphDescriptorIndex { engine_descriptor };
  }
};

} // namespace oxygen::examples::asyncsim
