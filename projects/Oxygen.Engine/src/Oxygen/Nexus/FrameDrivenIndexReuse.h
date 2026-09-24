//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at <https://opensource.org/licenses/BSD-3-Clause>.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <type_traits>
#include <variant>
#include <vector>

#include <Oxygen/Graphics/Common/Detail/DeferredReclaimer.h>
#include <Oxygen/Nexus/IndexReuse.h>

namespace oxygen::nexus {

//! Frame-completion adapter for the completion-independent slot lifecycle.
/*!
 Activation prepares all release storage before exposing a live handle. Owners
 close the adapter on the callback thread before destroying callback targets.
*/
template <IndexLike IndexType, typename ContextType = std::monostate>
class FrameDrivenIndexReuse {
  static_assert(std::is_nothrow_move_constructible_v<ContextType>);

public:
  using RecycleFn = std::function<void(IndexType, ContextType)>;
  using TelemetrySnapshot = IndexReuseTelemetry;

  FrameDrivenIndexReuse(
    graphics::detail::DeferredReclaimer& reclaimer, RecycleFn on_recycle)
    : reclaimer_(&reclaimer)
    , target_(std::make_shared<RecycleTarget>(std::move(on_recycle)))
  {
  }
  ~FrameDrivenIndexReuse() { Close(); }
  OXYGEN_MAKE_NON_COPYABLE(FrameDrivenIndexReuse)
  OXYGEN_MAKE_NON_MOVABLE(FrameDrivenIndexReuse)

  auto ActivateSlot(IndexType index) -> VersionedIndex<IndexType>
  {
    const auto raw = detail::GetIndexValue(index);
    if (raw >= (std::numeric_limits<uint32_t>::max)()) {
      throw std::out_of_range("Invalid reusable slot index");
    }
    std::lock_guard lock(mutex_);
    if (target_->closed.load(std::memory_order_acquire)) {
      throw std::logic_error("Frame reuse adapter is closed");
    }
    if (raw < activations_.size() && activations_[raw].payload) {
      throw std::logic_error("Slot is already active");
    }
    auto payload = std::make_shared<Payload>();
    auto action = reclaimer_->PrepareDeferredAction(
      [payload, target = target_]() noexcept {
        const auto result = payload->ticket->Finalize();
        if (result.disposition == FinalizeDisposition::kReusable
          && !target->closed.load(std::memory_order_acquire)
          && target->recycle) {
          target->recycle(*result.index, std::move(*payload->context));
        }
      });
    if (raw >= activations_.size()) {
      activations_.resize(
        (std::max)({ std::size_t { 64 }, raw + 1, activations_.size() * 2 }));
    }
    const auto handle = core_.ActivateSlot(index);
    activations_[raw]
      = Activation { handle, std::move(payload), std::move(action) };
    return handle;
  }

  auto Release(VersionedIndex<IndexType> handle, ContextType context) noexcept
    -> void
  {
    std::lock_guard lock(mutex_);
    auto retired = core_.TryRetire(handle);
    if (!retired) {
      return;
    }
    auto& activation = activations_[detail::GetIndexValue(handle.index)];
    activation.payload->ticket.emplace(std::move(*retired));
    activation.payload->context.emplace(std::move(context));
    // The action owns its payload after publication. Clear the activation
    // before a callback can reactivate this index.
    activation.payload.reset();
    reclaimer_->CommitDeferredAction(std::move(activation.action));
  }
  auto Release(VersionedIndex<IndexType> handle) noexcept -> void
    requires(std::is_nothrow_default_constructible_v<ContextType>)
  {
    Release(handle, ContextType {});
  }
  [[nodiscard]] auto IsHandleCurrent(
    VersionedIndex<IndexType> handle) const noexcept -> bool
  {
    return core_.IsHandleCurrent(handle);
  }
  auto OnBeginFrame(frame::Slot slot) -> void
  {
    reclaimer_->OnBeginFrame(slot);
  }
  auto Close() noexcept -> void
  {
    std::lock_guard lock(mutex_);
    target_->closed.store(true, std::memory_order_release);
    core_.Close();
  }
  [[nodiscard]] auto GetTelemetrySnapshot() const noexcept -> TelemetrySnapshot
  {
    return core_.GetTelemetrySnapshot();
  }

private:
  struct RecycleTarget {
    explicit RecycleTarget(RecycleFn fn)
      : recycle(std::move(fn))
    {
    }
    RecycleFn recycle;
    std::atomic<bool> closed { false };
  };
  struct Payload {
    std::optional<RetirementTicket<IndexType>> ticket;
    std::optional<ContextType> context;
  };
  struct Activation {
    VersionedIndex<IndexType> handle;
    std::shared_ptr<Payload> payload;
    graphics::detail::DeferredReclaimer::PreparedDeferredAction action;
  };
  graphics::detail::DeferredReclaimer* reclaimer_;
  std::shared_ptr<RecycleTarget> target_;
  IndexReuse<IndexType> core_;
  mutable std::mutex mutex_;
  std::vector<Activation> activations_;
};

} // namespace oxygen::nexus
