//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

#include <Oxygen/Base/Macros.h>

namespace oxygen::examples::asyncsim {

//! Global descriptor allocator for bindless resource management
class GlobalDescriptorAllocator {
public:
  // TODO: Implement descriptor heap management
  // Lock-free bump allocation with versioned publication
  uint32_t AllocateDescriptor() const { return next_descriptor_++; }
  void PublishDescriptorTable(uint64_t /*version*/) const
  {
    // TODO: Atomic publication with monotonic version bump
  }

private:
  mutable std::atomic<uint32_t> next_descriptor_ { 1 };
};

//! Global resource registry for bindless access
class GlobalResourceRegistry {
public:
  // TODO: Implement resource handle management
  // Generation-based handles with atomic registration
  uint64_t RegisterResource(const std::string& /*name*/) const
  {
    return next_handle_++;
  }
  void UnregisterResource(uint64_t /*handle*/) const
  {
    // TODO: Mark for deferred destruction
  }

private:
  mutable std::atomic<uint64_t> next_handle_ { 1 };
};

//! Deferred resource reclaimer with frame-based safety
class DeferredReclaimer {
public:
  struct ReclaimEntry {
    uint64_t resource_handle;
    uint64_t submitted_frame;
    std::string debug_name;
  };

  void ScheduleReclaim(
    uint64_t handle, uint64_t frame, const std::string& name);
  size_t ProcessCompletedFrame(uint64_t completed_frame);
  size_t GetPendingCount() const;

private:
  mutable std::mutex pending_mutex_;
  std::vector<ReclaimEntry> pending_reclaims_;
};

//! Graphics layer owning global systems
class GraphicsLayer {
public:
  GraphicsLayer() = default;
  ~GraphicsLayer() = default;

  OXYGEN_MAKE_NON_COPYABLE(GraphicsLayer)
  OXYGEN_MAKE_NON_MOVABLE(GraphicsLayer)

  // Global system accessors
  GlobalDescriptorAllocator& GetDescriptorAllocator()
  {
    return descriptor_allocator_;
  }
  GlobalResourceRegistry& GetResourceRegistry() { return resource_registry_; }
  DeferredReclaimer& GetDeferredReclaimer() { return deferred_reclaimer_; }

  // Frame lifecycle management
  void BeginFrame(uint64_t /*frame_index*/);
  void EndFrame();

  //! Get the number of resources reclaimed in the last frame start
  std::size_t GetLastReclaimedCount() const { return last_reclaimed_count_; }

  //! Process completed frames - handles GPU polling and triggers reclamation
  //! Returns the number of resources reclaimed in this call
  std::size_t ProcessCompletedFrames();

  uint64_t GetCurrentFrame() const { return current_frame_; }

  //! Get default viewport for fallback scenarios
  struct Viewport {
    float x { 0.0f }, y { 0.0f }, width { 1920.0f }, height { 1080.0f };
    float min_depth { 0.0f }, max_depth { 1.0f };
  };

  Viewport GetDefaultViewport() const
  {
    return Viewport {}; // Return default 1920x1080 viewport
  }

private:
  //! Poll GPU for completion status (abstracts fence checking)
  std::uint64_t PollGPUCompletion() const;

  GlobalDescriptorAllocator descriptor_allocator_;
  GlobalResourceRegistry resource_registry_;
  DeferredReclaimer deferred_reclaimer_;
  uint64_t current_frame_ { 0 };
  uint64_t current_fence_ { 0 };
  std::atomic<uint64_t> completed_frame_ { 0 }; // Last frame that GPU completed
  // Resources reclaimed in last BeginFrame
  std::size_t last_reclaimed_count_ { 0 };
};

} // namespace oxygen::examples::asyncsim
