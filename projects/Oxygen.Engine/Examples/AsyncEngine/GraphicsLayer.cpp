//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include "GraphicsLayer.h"

#include <algorithm>
#include <thread>

#include "EngineTypes.h"

#include "Oxygen/Base/Logging.h"

namespace oxygen::engine::asyncsim {

void DeferredReclaimer::ScheduleReclaim(
  uint64_t handle, uint64_t frame, const std::string& name)
{
  std::scoped_lock lock(pending_mutex_);
  pending_reclaims_.push_back({ handle, frame, name });
  LOG_F(1, "[Graphics] Scheduled reclaim: {} (handle={}, frame={})", name,
    handle, frame);
}

void GraphicsLayer::PresentSurfaces(const std::vector<RenderSurface>& surfaces)
{
  // Simulate synchronous present for each surface in order
  for (size_t i = 0; i < surfaces.size(); ++i) {
    const auto& s = surfaces[i];
    LOG_F(1, "[Graphics] Presenting surface {} (index={})", s.name, i);
  }
}

size_t DeferredReclaimer::ProcessCompletedFrame(uint64_t completed_frame)
{
  std::scoped_lock lock(pending_mutex_);

  std::vector<ReclaimEntry> to_reclaim;
  auto it = std::remove_if(pending_reclaims_.begin(), pending_reclaims_.end(),
    [completed_frame, &to_reclaim](const ReclaimEntry& entry) {
      // Safe to reclaim if frame completed (1-frame safety delay is enough for
      // simulation)
      bool can_reclaim = completed_frame >= entry.submitted_frame;
      if (can_reclaim) {
        to_reclaim.push_back(entry);
      }
      return can_reclaim;
    });

  size_t reclaimed = std::distance(it, pending_reclaims_.end());
  pending_reclaims_.erase(it, pending_reclaims_.end());

  // Log each reclaimed resource
  for (const auto& entry : to_reclaim) {
    LOG_F(1,
      "[Graphics] Reclaimed: {} (handle={}, submitted_frame={}, "
      "completed_frame={})",
      entry.debug_name, entry.resource_handle, entry.submitted_frame,
      completed_frame);
  }

  return reclaimed;
}

size_t DeferredReclaimer::GetPendingCount() const
{
  std::scoped_lock lock(pending_mutex_);
  return pending_reclaims_.size();
}

void GraphicsLayer::BeginFrame(uint64_t frame_index)
{
  current_frame_ = frame_index;
  current_fence_ = frame_index * 1000; // Simulated fence value

  LOG_F(1, "[Graphics] BeginFrame {} (fence={})", frame_index, current_fence_);

  // Handle GPU completion polling internally - engine core doesn't need to know
  last_reclaimed_count_ = ProcessCompletedFrames();

  if (last_reclaimed_count_ > 0) {
    LOG_F(1, "[Graphics] Reclaimed {} resources during frame begin",
      last_reclaimed_count_);
  }
}

void GraphicsLayer::EndFrame()
{
  LOG_F(
    2, "[Graphics] EndFrame {} - resources submitted to GPU", current_frame_);

  // Frame end - resources are now submitted to GPU
  // GPU completion will be polled later via ProcessCompletedFrames()
}

std::size_t GraphicsLayer::ProcessCompletedFrames()
{
  // Poll GPU completion status (in real engine, this would check actual fence
  // values)
  std::uint64_t current_completed = PollGPUCompletion();
  std::uint64_t previous_completed
    = completed_frame_.load(std::memory_order_acquire);

  if (current_completed > previous_completed) {
    completed_frame_.store(current_completed, std::memory_order_release);
    LOG_F(1, "[Graphics] GPU completed frame {} (was {})", current_completed,
      previous_completed);
  }

  // Process any completed frames and reclaim resources
  std::size_t reclaimed
    = deferred_reclaimer_.ProcessCompletedFrame(current_completed);

  if (reclaimed > 0) {
    LOG_F(1, "[Graphics] Processed completed frames - reclaimed {} resources",
      reclaimed);
  }

  return reclaimed;
}

std::uint64_t GraphicsLayer::PollGPUCompletion() const
{
  // In real engine: check actual GPU fence values
  // For simulation: advance completion more predictably

  // GPU completes frames with a 2-frame delay (more realistic)
  // Frame N completes when we're executing frame N+2
  if (current_frame_ >= 2) {
    std::uint64_t completed_frame = current_frame_ - 2;
    LOG_F(3, "[Graphics] GPU simulation: frame {} completed (current={})",
      completed_frame, current_frame_);
    return completed_frame;
  }

  return 0; // No frames completed yet
}

} // namespace oxygen::engine::asyncsim
