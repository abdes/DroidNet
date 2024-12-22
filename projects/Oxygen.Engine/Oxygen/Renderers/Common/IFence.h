//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include "Oxygen/Base/Types.h"

namespace oxygen::renderer {


  class IFence
  {
  public:
    /// Creates a new instance of the fence, but does not initialize it.
    IFence() = default;

    virtual ~IFence() = default;

    /// Initialize the fence with an initial value.
    virtual void Initialize(uint64_t initial_value = 0) = 0;

    /// Safely release the resources used by the fence, after it is no longer
    /// used by the GPU.
    ///
    /// \note Following the general practice of deterministic resource
    /// management, the Release() method should be explicitly called before the
    /// destructor is invoked. However, it is recommended to have an
    /// implementation of Release() that guards against double calls, and to
    /// call it from the destructor if it has not been done yet.
    virtual void Release() noexcept = 0;

    /// Sets the fence to the specified value.
    ///
    /// \param [in] value New value to set the fence to. Must be greater than
    /// the current value of the fence.
    ///
    /// \note Use this method to change the fence value from the CPU side. Use
    /// the command queue to enqueue a signal command that will change the value
    /// on the GPU after all previously submitted commands are complete.
    ///
    /// \note This method is essential for normal operation, where the fence
    /// value is incremented to indicate progress on the CPU side, indicating
    /// that it has completed a certain task or that a certain condition has
    /// been met. value only moves forward, which is important for
    /// synchronization.
    virtual void Signal(uint64_t value) = 0;

    /// Waits until the fence reaches or exceeds the specified value, on the CPU
    /// side.
    ///
    /// \param [in] value The value that the fence is waiting for.
    ///
    /// \note The method blocks the execution of the calling thread until the
    /// fence's GetCompletedValue() reaches the specified value.
    virtual void Wait(uint64_t value) const = 0;

    /// Returns the current value (i.e. last completed value signaled by the
    /// GPU) of the fence.
    [[nodiscard]] virtual auto GetCompletedValue() const->uint64_t = 0;

    /// Resets the fence to the specified value.
    ///
    /// \param [in] value The value to reset the fence to.
    ///
    /// \note This method is useful for reinitializing the fence to a known
    /// state, especially in scenarios where the fence needs to be reused. It
    /// allows the fence to be reset to a specific value without the constraints
    /// of only moving forward.
    virtual void Reset(uint64_t value) = 0;
  };

}  // namespace oxygen::renderer
