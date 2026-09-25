//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at <https://opensource.org/licenses/BSD-3-Clause>.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <shared_mutex>
#include <unordered_map>

#include <Oxygen/Base/Macros.h>
#include <Oxygen/Base/NamedType.h>
#include <Oxygen/Graphics/Common/api_export.h>

namespace oxygen::graphics {
class BackendLifetime;
class CommandQueue;

//! Thread-scoped admission; nested factories share their outer operation lock.
class BackendOperation final {
public:
  BackendOperation() noexcept = default;
  OXGN_GFX_API ~BackendOperation();
  OXYGEN_MAKE_NON_COPYABLE(BackendOperation)
  OXYGEN_MAKE_NON_MOVABLE(BackendOperation)

private:
  friend class BackendLifetime;
  OXGN_GFX_API explicit BackendOperation(const BackendLifetime& lifetime);
  static thread_local BackendOperation* current_;
  const BackendLifetime* lifetime_ { nullptr };
  BackendOperation* previous_ { nullptr };
  std::shared_lock<std::shared_mutex> lock_;
};
using BackendIncarnationId
  = NamedType<uint64_t, struct BackendIncarnationIdTag, Comparable, Hashable>;
enum class BackendLifecycle : uint8_t {
  kActive,
  kClosing,
  kRetiring,
  kReleased
};

//! Shared admission gate and module pin; never owns Graphics or its queues.
class BackendLifetime final {
public:
  OXGN_GFX_API BackendLifetime();
  OXGN_GFX_API ~BackendLifetime();
  OXYGEN_MAKE_NON_COPYABLE(BackendLifetime)
  OXYGEN_MAKE_NON_MOVABLE(BackendLifetime)

  //! Install once, before the loader publishes the canonical backend owner.
  OXGN_GFX_API auto Install(
    BackendIncarnationId id, std::shared_ptr<void> module) -> void;
  [[nodiscard]] auto Id() const noexcept -> BackendIncarnationId { return id_; }
  [[nodiscard]] auto State() const noexcept -> BackendLifecycle
  {
    return state_.load(std::memory_order_acquire);
  }
  //! Hold through acquisition/issue, never through command recording or GPU
  //! wait.
  [[nodiscard]] OXGN_GFX_API auto AcquireOperation() const -> BackendOperation;
  //! Excludes new operations and waits for admitted CPU operations to return.
  [[nodiscard]] OXGN_GFX_API auto BeginClose() noexcept -> bool;
  OXGN_GFX_API auto FinishClose() noexcept -> void;
  [[nodiscard]] auto IsFaulted() const noexcept -> bool
  {
    return faulted_.load(std::memory_order_acquire);
  }
  [[nodiscard]] auto RecordingEpoch() const noexcept -> uint64_t
  {
    return recording_epoch_.load(std::memory_order_acquire);
  }
  OXGN_GFX_API auto MarkSubmissionFault() noexcept -> void;
  OXGN_GFX_API auto ClearSubmissionFault() noexcept -> void;
  OXGN_GFX_API auto SynchronizeOperations() -> void;
  auto RetainRecording() noexcept -> void
  {
    active_recordings_.fetch_add(1, std::memory_order_relaxed);
  }
  auto ReleaseRecording() noexcept -> void
  {
    active_recordings_.fetch_sub(1, std::memory_order_release);
  }
  [[nodiscard]] auto HasRecordings() const noexcept -> bool
  {
    return active_recordings_.load(std::memory_order_acquire) != 0;
  }
  OXGN_GFX_API auto RegisterQueue(
    uint64_t id, std::weak_ptr<CommandQueue> queue) -> void;
  [[nodiscard]] OXGN_GFX_API auto FindQueue(uint64_t id) const
    -> std::shared_ptr<CommandQueue>;

private:
  friend class BackendOperation;
  std::shared_ptr<void> module_;
  BackendIncarnationId id_ { 0 };
  mutable std::shared_mutex gate_;
  std::atomic<BackendLifecycle> state_ { BackendLifecycle::kActive };
  std::atomic<bool> faulted_ { false };
  std::atomic<uint64_t> recording_epoch_ { 0 };
  std::atomic<uint64_t> active_recordings_ { 0 };
  mutable std::mutex queue_mutex_;
  std::unordered_map<uint64_t, std::weak_ptr<CommandQueue>> queues_;
};
} // namespace oxygen::graphics
