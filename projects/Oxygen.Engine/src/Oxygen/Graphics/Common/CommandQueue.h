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
#include <Oxygen/Graphics/Common/Types/QueueRole.h>
#include <Oxygen/Graphics/Common/Types/ResourceStates.h>
#include <Oxygen/Graphics/Common/api_export.h>

namespace oxygen::graphics {

class CommandList;

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

  //! Wait up to a certain number of milliseconds, for the counter to reach or
  //! exceed the specified value, on the CPU side.
  /*!
    \param [in] value The awaited value.
    \param [in] timeout The maximum time to wait for the counter to reach the
    expected value.
  */
  virtual auto Wait(uint64_t value, std::chrono::milliseconds timeout) const
    -> void
    = 0;

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

  virtual auto Submit(std::shared_ptr<CommandList> command_list) -> void = 0;
  virtual auto Submit(std::span<std::shared_ptr<CommandList>> command_lists)
    -> void
    = 0;

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

private:
  mutable std::mutex known_resource_states_mutex_ {};
  std::unordered_map<NativeResource, ResourceStates> known_resource_states_ {};
};

} // namespace oxygen::graphics
