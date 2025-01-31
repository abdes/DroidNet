//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <chrono>

#include "Oxygen/Base/Macros.h"
#include "Oxygen/Base/Mixin.h"
#include "Oxygen/Base/MixinDisposable.h"
#include "Oxygen/Base/MixinInitialize.h"
#include "Oxygen/Base/MixinNamed.h"

namespace oxygen::graphics {

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
class SynchronizationCounter
    : public Mixin<SynchronizationCounter,
          Curry<MixinNamed, const char*>::mixin,
          MixinDisposable,
          MixinInitialize // last to consume remaining args
          > {
public:
    //! Constructor to forward the arguments to the mixins in the chain.
    template <typename... Args>
    constexpr explicit SynchronizationCounter(Args&&... args)
        : Mixin(std::forward<Args>(args)...)
    {
    }

    ~SynchronizationCounter() override = default;

    OXYGEN_MAKE_NON_COPYABLE(SynchronizationCounter);
    OXYGEN_MAKE_NON_MOVEABLE(SynchronizationCounter);

    //! Set the counter to the specified value on the CPU side.
    /*!
      \param [in] value The value to set the counter to. Must be greater than
      the current value. \note This method is useful in scenarios where command
      submission is done out of order, and synchronization is required at
      multiple discrete points in the command queue timeline.
    */
    virtual void Signal(uint64_t value) const = 0;

    //! Increment the counter current value on the CPU side by 1.
    //! \return The new counter value, to be used to wait for completion.
    [[nodiscard]] virtual auto Signal() const -> uint64_t = 0;

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
    virtual void QueueWaitCommand(uint64_t value) const = 0;

    //! Get the last completed value of the counter.
    //! \return  The last value signaled by the GPU.
    [[nodiscard]] virtual auto GetCompletedValue() const -> uint64_t = 0;

    //! Get the current value of the counter.
    //! \return  The last value signaled by the CPU.
    [[nodiscard]] virtual auto GetCurrentValue() const -> uint64_t = 0;

protected:
    virtual void InitializeSynchronizationObject(uint64_t initial_value) = 0;
    virtual void ReleaseSynchronizationObject() noexcept = 0;

private:
    void OnInitialize(const uint64_t initial_value)
    {
        if (this->self().ShouldRelease()) {
            const auto msg = fmt::format("{} OnInitialize() called twice without calling Release()", this->self().ObjectName());
            LOG_F(ERROR, "{}", msg);
            throw std::runtime_error(msg);
        }
        try {
            InitializeSynchronizationObject(initial_value);
            this->self().ShouldRelease(true);
        } catch (const std::exception& e) {
            LOG_F(ERROR, "Failed to initialize {}: {}", this->self().ObjectName(), e.what());
            throw;
        }
    }
    template <typename Base, typename... CtorArgs>
    friend class MixinInitialize; //< Allow access to OnInitialize.

    void OnRelease() noexcept
    {
        ReleaseSynchronizationObject();
        this->self().IsInitialized(false);
    }
    template <typename Base>
    friend class MixinDisposable; //< Allow access to OnRelease.
};

} // namespace oxygen::graphics
