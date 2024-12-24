//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <memory>

#include "Oxygen/Base/Macros.h"
#include "Oxygen/Renderers/Common/ISynchronizationCounter.h"

namespace oxygen::renderer::d3d12 {

  namespace detail {
    class CommanderImpl;
    class FenceImpl;
  }  // namespace detail

  class Fence final : public ISynchronizationCounter
  {
  public:
    using FenceImplPtr = std::unique_ptr<detail::FenceImpl>;

    ~Fence() override;

    OXYGEN_MAKE_NON_COPYABLE(Fence);
    OXYGEN_MAKE_NON_MOVEABLE(Fence);

    void Initialize(uint64_t initial_value = 0) override;
    void Release() noexcept override;

    void Signal(uint64_t value) override;
    [[nodiscard]] uint64_t Signal() override;
    void Wait(uint64_t value, std::chrono::milliseconds timeout) const override;
    void Wait(uint64_t value) const override;
    void QueueWaitCommand(uint64_t value) override;
    void QueueSignalCommand(uint64_t value) override;

    [[nodiscard]] auto GetCompletedValue() const->uint64_t override;

  private:
    friend class detail::CommanderImpl;
    explicit Fence(FenceImplPtr pimpl) : pimpl_{ std::move(pimpl) } {}

    uint64_t current_value_{ 0 };
    bool should_release_{ false };

    FenceImplPtr pimpl_{};
  };

}  // namespace oxygen::renderer::d3d12
