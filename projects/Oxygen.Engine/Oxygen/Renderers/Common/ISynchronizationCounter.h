//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include "Oxygen/Base/Types.h"

namespace oxygen::renderer {

  //! A synchronization counter, for a timeline oriented CPU-GPU command queue.
  /*!
    The command queue is viewed as a sequence of commands that happen over time
    (a timeline), and the counter is a way to synchronize the CPU and GPU on
    this timeline.

    To change the counter's value on the CPU side the `Signal()` methods are
    used, and onb the GPU side, the `QueueSignalCommand()` method.

    To wait for the counter to reach a specific value, the `Wait()` method is
    used on the CPU side, and the `QueueWaitCommand()` method on the GPU side.

    In a typical usage example, the `Signal()` method is called to increment the
    counter value on the CPU side by `1`, immediately followed by a call to
    `QueueSignalCommand()` with the returned value to queue a Signal command,
    and then finally wait for the work to be completed.

    A more advanced usage could would involve holding the GPU until the counter
    reaches a certain value on the GPU side, then signaling work completion. The
    interface leaves the flexibility of how to structure the work submission and
    execution to the user.
  */
  class ISynchronizationCounter
  {
  public:
    //! Creates a new instance of the fence, but does not initialize it.
    ISynchronizationCounter() = default;

    //! Destructor, may automatically call `Release()` if not done yet.
    virtual ~ISynchronizationCounter() = default;

    OXYGEN_MAKE_NON_COPYABLE(ISynchronizationCounter);
    OXYGEN_MAKE_NON_MOVEABLE(ISynchronizationCounter);

    //! Initialize the synchronization counter with an initial value.
    virtual void Initialize(uint64_t initial_value = 0) = 0;

    //! Safely release any resources used by the counter.
    /*!
      Implementations must ensure that any resources being released are no
      longer used by the GPU.

      \note Following the general practice of deterministic resource management,
      the Release() method should be explicitly called before the destructor is
      invoked. However, it is recommended to have an implementation of Release()
      that guards against double calls, and to call it from the destructor if it
      has not been done yet.
    */
    virtual void Release() noexcept = 0;

    //! Set the counter to the specified value on the CPU side.
    /*!
      \param [in] value The value to set the counter to. Must be greater than
      the current value. \note This method is useful in scenarios where command
      submission is done out of order, and synchronization is required at
      multiple discrete points in the command queue timeline.
    */
    virtual void Signal(uint64_t value) = 0;

    //! Increment the counter current value on the CPU side by 1.
    //! \return The new counter value, to be used to wait for completion.
    [[nodiscard]] virtual uint64_t Signal() = 0;

    //! Wait up to a certain number of milliseconds, for the counter to reach or
    //! exceed the specified value, on the CPU side.
    /*!
      \param [in] value The awaited value.
      \param [in] timeout The maximum time to wait for the counter to reach the
      expected value.
    */
    virtual void Wait(uint64_t value, std::chrono::milliseconds timeout) const = 0;


    //! Wait for as long as it takes, for the counter to reach or exceed the
    //! specified value, on the CPU side.
    //! \param [in] value The awaited value.
    virtual void Wait(uint64_t value) const = 0;

    //! Enqueue a command to set the counter to the specified value on the GPU
    //! side.
    //! \param [in] value The value to set the counter to.
    virtual void QueueSignalCommand(uint64_t value) = 0;

    //! Enqueue a command to hold the GPU for the counter to reach or exceed the
    //! specified value.
    //! \param [in] value The value that the GPU should be waiting for.
    virtual void QueueWaitCommand(uint64_t value) = 0;

    //! Get the last completed value of the counter.
    //! \return  The last value signaled by the GPU.
    [[nodiscard]] virtual auto GetCompletedValue() const->uint64_t = 0;
  };

}  // namespace oxygen::renderer
