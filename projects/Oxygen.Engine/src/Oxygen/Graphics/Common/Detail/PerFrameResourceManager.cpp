//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Graphics/Common/Detail/PerFrameResourceManager.h>

using oxygen::graphics::detail::PerFrameResourceManager;

void PerFrameResourceManager::OnBeginFrame(const frame::Slot frame_slot)
{
  current_frame_slot_.store(frame_slot.get(), std::memory_order_release);
  ReleaseDeferredResources(frame_slot);
}

void PerFrameResourceManager::ProcessAllDeferredReleases()
{
  DLOG_F(INFO, "Releasing all deferred resource for all frames...");
  for (uint32_t i = 0; i < frame::kFramesInFlight.get(); ++i) {
    ReleaseDeferredResources(frame::Slot { i });
  }
}

void PerFrameResourceManager::OnRendererShutdown()
{
  ProcessAllDeferredReleases();
}

void PerFrameResourceManager::ReleaseDeferredResources(
  const frame::Slot frame_slot)
{
  const auto u_frame_slot = frame_slot.get();

  // Acquire lock, swap vector with a local one and release lock so that the
  // callbacks can run without holding the mutex. This allows worker threads
  // to register actions concurrently.
  std::vector<std::function<void()>> local_releases;
  {
    std::lock_guard<std::mutex> lock(deferred_mutexes_[u_frame_slot]);
    local_releases = std::move(deferred_releases_[u_frame_slot]);
    deferred_releases_[u_frame_slot].clear();
  }

#if !defined(NDEBUG)
  if (!local_releases.empty()) {
    LOG_SCOPE_FUNCTION(2);
    DLOG_F(2, "Frame [{}]", frame_slot);
    DLOG_F(2, "{} objects to release", local_releases.size());
  }
#endif // NDEBUG

  for (auto& release : local_releases) {
    release();
  }
}

/*!
 Registers an arbitrary action to be executed when the observed frame slot
 cycles.

 @param action The callable to execute. The action will be invoked from the
               thread that runs OnBeginFrame for the observed frame slot.

 ### Performance Characteristics

 - Time Complexity: O(1) amortized for enqueue.
 - Memory: allocates into the per-frame vector; short-lived allocations when
   vectors grow.

 ### Usage Examples

 ```cpp
 // From a worker thread:
 per_frame_manager.RegisterDeferredAction([]() {
   // cleanup that must run on the renderer thread
 });
 ```

 @note This method is thread-safe: it reads the current frame index with
       acquire semantics and appends the action under a per-bucket mutex.

 @warning If the frame index changes concurrently, the action may be placed
          into either the previous or new frame bucket depending on the
          observed index. Callbacks execute on the renderer thread and must
          not block for long periods.

 @see OnBeginFrame, ProcessAllDeferredReleases
*/
void PerFrameResourceManager::RegisterDeferredAction(
  std::function<void()> action)
{
  auto frame_idx = current_frame_slot_.load(std::memory_order_acquire);
  auto& bucket = deferred_releases_[frame_idx];
  {
    std::lock_guard<std::mutex> lock(deferred_mutexes_[frame_idx]);
    bucket.emplace_back(std::move(action));
  }
}
