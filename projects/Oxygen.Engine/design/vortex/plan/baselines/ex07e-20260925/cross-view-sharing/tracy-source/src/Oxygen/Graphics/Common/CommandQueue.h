//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <chrono>
#include <cstdint>
#include <mutex>
#include <optional>
#include <span>
#include <string_view>
#include <unordered_map>

#include <Oxygen/Base/Macros.h>
#include <Oxygen/Composition/Composition.h>
#include <Oxygen/Composition/Named.h>
#include <Oxygen/Graphics/Common/NativeObject.h>
#include <Oxygen/Graphics/Common/Submission.h>
#include <Oxygen/Graphics/Common/Types/QueueRole.h>
#include <Oxygen/Graphics/Common/Types/ResourceStates.h>
#include <Oxygen/Graphics/Common/api_export.h>

namespace oxygen {
class Graphics;
}
namespace oxygen::graphics {

class CommandList;
class ResourceRegistry;
namespace internal {
  struct NativeSubmissionRequest;
  class NativeSubmission;
  struct SubmittedWork;
  class SubmissionFaultTestAccess;
}

class CommandQueue : public Composition, public Named {
public:
  struct KnownResourceState {
    NativeResource resource {};
    ResourceStates state { ResourceStates::kUnknown };
  };

  OXGN_GFX_API explicit CommandQueue(std::string_view name);

  OXGN_GFX_API ~CommandQueue() override;

  OXYGEN_MAKE_NON_COPYABLE(CommandQueue)
  OXYGEN_MAKE_NON_MOVABLE(CommandQueue)

  //! Reserve the queue timeline counter at the specified value on the CPU side.
  /*!
    \param [in] value The value to set the counter to. Must be greater
   * than
    the current value. This does not enqueue any GPU work. Use

   * `CommandRecorder::RecordQueueSignal(...)` to emit a submit-ordered
   * GPU-side
    signal for recorded work.
  */
  virtual auto Signal(uint64_t value) const -> void = 0;

  //! Reserve the next queue timeline value on the CPU side.
  //! \return The reserved value, to be used for submit-ordered signaling.
  [[nodiscard]] virtual auto Signal() const -> uint64_t = 0;

  //! Enqueue a completion marker after all work already submitted to this
  //! queue. The frame owner serializes this call with submission. Unlike
  //! Flush(), this does not wait on the CPU; the returned value protects
  //! subsequent reuse.
  [[nodiscard]] OXGN_GFX_API virtual auto SignalSubmittedWork() -> uint64_t;

  //! Wait up to a certain number of milliseconds, for the counter to reach or
  //! exceed the specified value, on the CPU side.
  /*!
    \param [in] value The awaited value.
    \param [in] timeout The maximum time to wait for the counter to reach the
    expected value.
  */
  virtual auto Wait(uint64_t value, std::chrono::milliseconds timeout) const
    -> void = 0;

  //! Wait for as long as it takes, for the counter to reach or exceed the
  //! specified value, on the CPU side.
  //! \param [in] value The awaited value.
  virtual auto Wait(uint64_t value) const -> void = 0;

  //! Get the last completed value of the counter.
  //! \return  The last value signaled by the GPU.
  [[nodiscard]] virtual auto GetCompletedValue() const -> uint64_t = 0;

  //! Get the current value of the counter.
  //! \return  The last value signaled by the CPU.
  [[nodiscard]] virtual auto GetCurrentValue() const -> uint64_t = 0;

  //! Query the queue timestamp frequency when the backend supports timestamp
  //! queries on this queue.
  OXGN_GFX_API virtual auto TryGetTimestampFrequency(uint64_t& out_hz) const
    -> bool;

  OXGN_GFX_API virtual auto Submit(std::shared_ptr<CommandList> command_list)
    -> void;
  OXGN_GFX_API virtual auto Submit(
    std::span<std::shared_ptr<CommandList>> command_lists) -> void;
  [[nodiscard]] OXGN_GFX_API auto SubmitWithReceipt(
    std::shared_ptr<CommandList> command_list) noexcept -> SubmissionResult;
  [[nodiscard]] OXGN_GFX_API auto Identity() const noexcept -> QueueIdentity;
  [[nodiscard]] OXGN_GFX_API auto BackendLifetimeState() const noexcept
    -> std::shared_ptr<BackendLifetime>;
  OXGN_GFX_API auto BindBackend(std::shared_ptr<BackendLifetime> lifetime,
    std::shared_ptr<void> native_lifetime) -> void;
  [[nodiscard]] OXGN_GFX_API auto QueryCompletion(
    CompletionReceipt receipt) const noexcept -> CompletionStatus;
  OXGN_GFX_API auto PollCompletedUses() -> void;
  [[nodiscard]] OXGN_GFX_API auto HasStateConflictWith(
    const CommandQueue& other) const -> bool;
  OXGN_GFX_API auto ReleaseUsesAfterDeviceLoss() noexcept -> void;

  //! Advance backend-owned profiling frame state before a new engine frame.
  /*!
   Backends with queue-scoped profiler state can override this to roll
   per-frame query batches or similar bookkeeping. The default implementation
   is a no-op.
  */
  OXGN_GFX_API virtual auto BeginProfilingFrame() const -> void;
  virtual OXGN_GFX_API auto Flush() const -> void;

  OXGN_GFX_NDAPI auto TryGetKnownResourceState(
    const NativeResource& resource) const -> std::optional<ResourceStates>;
  OXGN_GFX_API auto AdoptKnownResourceStates(
    std::span<const KnownResourceState> states) -> void;
  OXGN_GFX_API auto ForgetKnownResourceState(const NativeResource& resource)
    -> void;

  [[nodiscard]] virtual auto GetQueueRole() const -> QueueRole = 0;

  OXGN_GFX_NDAPI auto GetName() const noexcept -> std::string_view override;
  OXGN_GFX_API auto SetName(std::string_view name) noexcept -> void override;

protected:
  //! All CPU/native storage is prepared before the backend can issue work.
  [[nodiscard]] OXGN_GFX_API virtual auto PrepareNativeSubmission(
    const internal::NativeSubmissionRequest& request,
    std::unique_ptr<internal::NativeSubmission> reusable)
    -> std::unique_ptr<internal::NativeSubmission>;
  [[nodiscard]] OXGN_GFX_API virtual auto
  QueryPrivateCompletion() const noexcept -> uint64_t;
  OXGN_GFX_API virtual auto WaitPrivateCompletion(uint64_t value) const -> void;
  OXGN_GFX_API virtual auto TearDownUncertainDevice() noexcept -> void;
  OXGN_GFX_API virtual auto WaitForNativeStop() noexcept -> void;
  //! Emit an immediate queue-side signal for backend-owned synchronization.
  /*!
    This is intentionally a backend-only primitive. Application code
   * should use
    `Signal()` to reserve a value and
   * `CommandRecorder::RecordQueueSignal(...)`
    to associate that value with
   * recorded work.
  */
  virtual auto SignalImmediate(uint64_t value) const -> void = 0;
  OXGN_GFX_API virtual auto EnqueueLegacyMarker(uint64_t value) -> void;

private:
  friend class internal::SubmissionFaultTestAccess;
  friend class oxygen::Graphics;
  [[nodiscard]] OXGN_GFX_API auto ReconcileQuarantinedStates() -> bool;
  [[nodiscard]] OXGN_GFX_API auto RecoverQuarantinedWork(
    ResourceRegistry& registry) -> bool;
  OXGN_GFX_API auto StopAfterSubmissionFailure() noexcept -> void;
  struct SubmissionState;
  std::unique_ptr<SubmissionState> submission_;
  auto EmitCompletionMarker() -> uint64_t;
  auto SubmitPrepared(std::span<const std::shared_ptr<CommandList>> lists,
    bool request_receipt) noexcept -> SubmissionResult;
  auto RecycleWork(std::unique_ptr<internal::SubmittedWork> work,
    std::optional<UseReleaseReason> completion) noexcept -> void;
  auto PrepareStateAdoption(const internal::SubmittedWork& work) -> void;
  auto CommitStateAdoption(
    const internal::SubmittedWork& work, size_t issued_lists) noexcept -> void;
  mutable std::mutex known_resource_states_mutex_ {};
  std::unordered_map<NativeResource, ResourceStates> known_resource_states_ {};
};

} // namespace oxygen::graphics
