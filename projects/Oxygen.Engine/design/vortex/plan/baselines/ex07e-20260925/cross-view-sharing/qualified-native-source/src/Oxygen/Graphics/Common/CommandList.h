//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstdint>
#include <string_view>
#include <vector>

#include <Oxygen/Base/Macros.h>
#include <Oxygen/Composition/Composition.h>
#include <Oxygen/Composition/Named.h>
#include <Oxygen/Graphics/Common/NativeObject.h>
#include <Oxygen/Graphics/Common/RecordingUseBatch.h>
#include <Oxygen/Graphics/Common/Types/QueueRole.h>
#include <Oxygen/Graphics/Common/Types/ResourceStates.h>
#include <Oxygen/Graphics/Common/api_export.h>

namespace oxygen::graphics {
namespace internal {
  struct SubmittedWork;
}

class CommandList : public Composition, public Named {
public:
  struct RecordedResourceState {
    NativeResource resource {};
    ResourceStates state { ResourceStates::kUnknown };
  };

  enum class SubmitQueueActionKind : uint8_t {
    kWait,
    kSignal,
  };

  struct SubmitQueueAction {
    SubmitQueueActionKind kind;
    uint64_t value;
  };

  OXGN_GFX_API CommandList(std::string_view name, QueueRole type);

  //! Destroys the command list after releasing all graphics resources it was
  //! using.
  /*!
   \note It is the responsibility of the user to ensure the command list (or
   its associated resources) are not in use by ongoing GPU operations.
  */
  OXGN_GFX_API ~CommandList() override;

  OXYGEN_MAKE_NON_COPYABLE(CommandList)
  OXYGEN_MAKE_NON_MOVABLE(CommandList)

  OXGN_GFX_NDAPI auto GetQueueRole() const { return type_; }

  OXGN_GFX_NDAPI auto GetName() const noexcept -> std::string_view override;
  OXGN_GFX_API auto SetName(std::string_view name) noexcept -> void override;

  // State query methods
  [[nodiscard]] auto IsFree() const noexcept { return state_ == State::kFree; }
  [[nodiscard]] auto IsRecording() const noexcept
  {
    return state_ == State::kRecording;
  }
  [[nodiscard]] auto IsClosed() const noexcept
  {
    return state_ == State::kClosed;
  }
  [[nodiscard]] auto IsSubmitted() const noexcept
  {
    return state_ == State::kSubmitted;
  }

  OXGN_GFX_API virtual auto OnBeginRecording() -> void;
  OXGN_GFX_API virtual auto OnEndRecording() -> void;
  OXGN_GFX_API virtual auto OnSubmitted() -> void;
  OXGN_GFX_API virtual auto OnExecuted() -> void;
  OXGN_GFX_API virtual auto OnFailed() noexcept -> void;
  //! Native recording failure makes this list unsuitable for pool reuse.
  OXGN_GFX_API auto Invalidate() noexcept -> void;
  [[nodiscard]] auto Uses() noexcept -> RecordingUseBatch& { return uses_; }
  [[nodiscard]] auto Uses() const noexcept -> const RecordingUseBatch&
  {
    return uses_;
  }
  OXGN_GFX_API auto BindBackend(
    std::shared_ptr<BackendLifetime> lifetime, QueueIdentity queue) -> void;
  [[nodiscard]] OXGN_GFX_API auto RecordingIsCurrent() const noexcept -> bool;
  [[nodiscard]] auto SubmitActions() const noexcept
    -> std::span<const SubmitQueueAction>
  {
    return submit_queue_actions_;
  }
  [[nodiscard]] auto RecordedStates() const noexcept
    -> std::span<const RecordedResourceState>
  {
    return recorded_resource_states_;
  }
  OXGN_GFX_API auto QueueSubmitSignal(uint64_t value) -> void;
  OXGN_GFX_API auto QueueSubmitWait(uint64_t value) -> void;
  [[nodiscard]] OXGN_GFX_API auto HasSubmitQueueActions() const noexcept
    -> bool;
  OXGN_GFX_API auto TakeSubmitQueueActions() -> std::vector<SubmitQueueAction>;
  OXGN_GFX_API auto SetRecordedResourceStates(
    std::vector<RecordedResourceState> states) -> void;
  OXGN_GFX_API auto TakeRecordedResourceStates()
    -> std::vector<RecordedResourceState>;

  enum class State : int8_t {
    kInvalid = -1, //<! Invalid state

    kFree = 0, //<! Free command list.
    kRecording = 1, //<! The command list is being recorded.
    kClosed = 2, //<! The command list is recorded and ready to be submitted.
    kExecutionUncertain = 4, //<! Native issue has no completion proof.
    kSubmitted = 3, //<! The command list is being executed.
  };
  [[nodiscard]] auto GetState() const { return state_; }

private:
  friend class CommandQueue;
  auto TakeRetirementStorage() -> std::unique_ptr<internal::SubmittedWork>;
  auto RestoreRetirementStorage(
    std::unique_ptr<internal::SubmittedWork> storage) noexcept -> void;
  std::weak_ptr<BackendLifetime> backend_lifetime_;
  bool backend_bound_ { false };
  uint64_t recording_epoch_ { 0 };
  RecordingUseBatch uses_;
  std::unique_ptr<internal::SubmittedWork> retirement_storage_;
  QueueRole type_;
  State state_ { State::kInvalid };
  std::vector<SubmitQueueAction> submit_queue_actions_ {};
  std::vector<RecordedResourceState> recorded_resource_states_ {};
};

} // namespace oxygen::graphics
