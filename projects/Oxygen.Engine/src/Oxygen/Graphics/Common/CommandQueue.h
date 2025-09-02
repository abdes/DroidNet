//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <chrono>
#include <cstdint>
#include <span>
#include <string_view>

#include <Oxygen/Base/Macros.h>
#include <Oxygen/Composition/Composition.h>
#include <Oxygen/Composition/Named.h>
#include <Oxygen/Graphics/Common/Types/QueueRole.h>
#include <Oxygen/Graphics/Common/api_export.h>

namespace oxygen::graphics {

class CommandList;

class CommandQueue : public Composition, public Named {
public:
  OXGN_GFX_API explicit CommandQueue(std::string_view name);

  OXGN_GFX_API ~CommandQueue() override;

  OXYGEN_MAKE_NON_COPYABLE(CommandQueue)
  OXYGEN_MAKE_NON_MOVABLE(CommandQueue)

  //! Set the counter to the specified value on the CPU side.
  /*!
    \param [in] value The value to set the counter to. Must be greater than
    the current value. \note This method is useful in scenarios where command
    submission is done out of order, and synchronization is required at
    multiple discrete points in the command queue timeline.
  */
  virtual auto Signal(uint64_t value) const -> void = 0;

  //! Increment the current value on the CPU side by 1.
  //! \return The new value, to be used to wait for completion.
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

  //! Enqueue a command to set the counter to the specified value on the GPU
  //! side.
  //! \param [in] value The value to set the counter to.
  virtual auto QueueSignalCommand(uint64_t value) -> void = 0;

  //! Enqueue a command to hold the GPU for the counter to reach or exceed the
  //! specified value.
  //! \param [in] value The value that the GPU should be waiting for.
  virtual auto QueueWaitCommand(uint64_t value) const -> void = 0;

  //! Get the last completed value of the counter.
  //! \return  The last value signaled by the GPU.
  [[nodiscard]] virtual auto GetCompletedValue() const -> uint64_t = 0;

  //! Get the current value of the counter.
  //! \return  The last value signaled by the CPU.
  [[nodiscard]] virtual auto GetCurrentValue() const -> uint64_t = 0;

  virtual auto Submit(std::shared_ptr<CommandList> command_list) -> void = 0;
  virtual auto Submit(std::span<std::shared_ptr<CommandList>> command_lists)
    -> void
    = 0;

  virtual OXGN_GFX_API auto Flush() const -> void;

  [[nodiscard]] virtual auto GetQueueRole() const -> QueueRole = 0;

  OXGN_GFX_NDAPI auto GetName() const noexcept -> std::string_view override;
  OXGN_GFX_API auto SetName(std::string_view name) noexcept -> void override;
};

} // namespace oxygen::graphics
